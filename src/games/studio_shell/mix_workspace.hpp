// =============================================================================
//  games/studio_shell/mix_workspace.hpp  —  the Studio's parts mixer
// =============================================================================
//  The FOURTH workspace, and the first one whose job is to make an asset rather than
//  to edit one. Map, Scene and Pixels all open a thing that exists and change it; the
//  Mixer opens a handful of PARTS and produces a character that nobody drew.
//
//  That is why it is worth a workspace of its own rather than a mode of the Pixel
//  editor. The two answer different questions. Pixels asks "is this pixel right"; the
//  Mixer asks "which of these do I want, and in what colour" — and the second question
//  has a small, closed set of answers, which is exactly the constraint that makes the
//  output consistent. A brush would let you draw a villager who does not match the
//  village. Parts cannot.
//
//  Undo is by WHOLE-DOCUMENT SNAPSHOT, like the Scene workspace: a `.mix` is a few
//  hundred bytes, `mix::to_text` already round-trips it, so a command is a pair of
//  strings — trivially correct, no per-edit inverse, idempotent. ponytail: O(n) text
//  per edit, which at this size is nothing.
//
//  Two verbs, and they are NOT the same one: `Save` writes the `.mix` (the source),
//  and `Bake` writes the `.hrt` (the artefact, through the same `asset.mix` command
//  the CLI runs). Collapsing them would make every keystroke rewrite a committed
//  binary, and would hide the fact that the source is the thing under version control.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/document/command_stack.hpp"
#include "engine/image.hpp"
#include "engine/mix/mix.hpp"
#include "games/studio_shell/workspace.hpp"

namespace studioshell {

class MixWorkspace : public Workspace {
public:
    // `texture_paths` are the project's declared `.hrt` assets. The ones this can open
    // are those with a sibling `.mix` — which is the same rule `provenance_core` uses
    // to call an asset `mixed`, rather than a second list that can disagree with it.
    explicit MixWorkspace(std::vector<std::string> texture_paths);
    ~MixWorkspace() override;

    [[nodiscard]] const char*        name() const override { return "Mixer"; }
    [[nodiscard]] bool               loaded() const override { return loaded_; }
    [[nodiscard]] const std::string& path() const override { return path_; }
    [[nodiscard]] const std::string& problem() const override { return problem_; }
    [[nodiscard]] bool               dirty() const override { return stack_.dirty(); }
    [[nodiscard]] std::string        status() const override;
    [[nodiscard]] const char*        hint() const override;
    [[nodiscard]] int inspector_width() const override { return 280; }

    void register_commands() override;
    void update(double dt, const platform::InputState& in, bool interactive) override;
    void draw_canvas(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) override;
    void draw_inspector(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) override;

    engine::OpResult save() override;     // the SOURCE
    engine::OpResult reload() override;
    std::optional<engine::OpResult> take_message() override;

    [[nodiscard]] bool recovery_pending() const override { return recovery_pending_; }
    void take_recovery() override;
    void dismiss_recovery() override;

    // ---- exposed for the host and for tests ---------------------------------
    [[nodiscard]] const mix::Mix&    doc() const { return mix_; }
    [[nodiscard]] const gfx::Image&  preview() const { return preview_; }
    [[nodiscard]] const std::vector<std::string>& mixes() const { return paths_; }
    [[nodiscard]] std::size_t part_count() const { return mix_.parts.size(); }

    // Add the sheet tile `index` as a new part on top, at 0,0. One undo step.
    engine::OpResult add_part(int index);
    // Drop the topmost part. Refuses to leave the mix empty — a mix that composes
    // nothing is not a document, it is a file somebody abandoned.
    engine::OpResult remove_top();
    // Swap the colour under the cursor for the next one in the ramp, or undo an
    // existing swap of it. Refuses on a transparent pixel: there is no colour there.
    engine::OpResult cycle_swap(gfx::Color from);
    // Write the `.hrt` the source describes, through the same command the CLI runs.
    engine::OpResult bake();
    // Open the next mix in the project. Refuses while dirty, like the Pixel workspace:
    // switching documents is not a place to lose work quietly.
    engine::OpResult next_mix();

    // Where an inspector control landed in the last draw, by name; empty when it was
    // not drawn. Same seam as the Scene workspace, for the same reason: a test must
    // press what a hand presses.
    [[nodiscard]] ui::Rect control_rect(const char* id) const;
    // The screen rect of tile `i` of the parts strip, and of preview pixel (x,y).
    [[nodiscard]] ui::Rect part_rect(int index) const;
    [[nodiscard]] ui::Rect preview_pixel_rect(int x, int y) const;
    [[nodiscard]] int      sheet_tiles() const;

private:
    void load();
    void note(bool ok, std::string msg);
    void install(const std::string& text);     // no history — load, undo, reload
    void commit(const std::string& before, std::string label);
    void recompose();                          // preview_ from mix_ + sheets_
    void mark(const char* id, ui::Rect r);
    [[nodiscard]] const mix::Mix::Sheet* first_sheet() const;

    std::vector<std::string> paths_;            // the `.mix` files this can open
    std::size_t              which_ = 0;
    std::string              path_, problem_;
    bool                     loaded_ = false;

    mix::Mix          mix_{};
    doc::CommandStack stack_;
    gfx::Image        preview_{};
    std::unordered_map<std::string, gfx::Image> sheets_;   // by sheet NAME

    bool        recovery_pending_ = false;
    std::string recovery_text_;

    gfx::Color picked_ = 0;        // the colour the last canvas click sampled
    ui::Rect   canvas_{}, strip_{}, preview_rect_{};
    int        preview_scale_ = 1, strip_scale_ = 1;
    std::unordered_map<std::string, ui::Rect> controls_;

    double autosave_timer_ = 0.0;
    bool   commands_registered_ = false;
    std::optional<engine::OpResult> message_;

    int  want_part_ = -1;
    bool want_remove_ = false, want_save_ = false, want_bake_ = false, want_swap_ = false;
    bool want_undo_ = false, want_redo_ = false, want_next_ = false;
};

} // namespace studioshell
