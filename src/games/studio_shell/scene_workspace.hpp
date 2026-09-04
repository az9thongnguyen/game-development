// =============================================================================
//  games/studio_shell/scene_workspace.hpp  —  the Studio's actor/scene editor
// =============================================================================
//  The sandbox (`--sandbox`) as a Workspace. It was a standalone Scene that drew
//  itself over the whole window at hardcoded coordinates, still used the pre-chapter
//  -109 widget set, did its input handling inside render(), and had **no undo** — the
//  one thing chapter 111 built a CommandStack for.
//
//  Absorbing it rather than writing a second editor is the same rule as everywhere
//  else here: two implementations of "place an actor and drag it" would agree today
//  and diverge later. `--sandbox` still exists and is now a thin host that runs THIS
//  workspace full-screen, so there is one implementation with two frames around it.
//
//  Undo is by WHOLE-SCENE SNAPSHOT. sandbox::to_scene/from_scene already round-trip
//  the world, so a command is a pair of scene strings — which is trivially correct,
//  needs no per-edit inverse, and is idempotent (D21), so a drag can be committed
//  after it has already happened. ponytail: O(n) text per edit; that is nothing at
//  tens of actors and would matter at thousands, at which point the fix is per-edit
//  commands, not a cleverer snapshot.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/document/command_stack.hpp"
#include "engine/image.hpp"
#include "games/sandbox/world.hpp"
#include "games/studio_shell/workspace.hpp"

namespace studioshell {

class SceneWorkspace : public Workspace {
public:
    // `scene_path` is asset-relative and may be empty (the project declares no scene).
    explicit SceneWorkspace(std::string scene_path);
    ~SceneWorkspace() override;

    [[nodiscard]] const char*        name() const override { return "Scene"; }
    [[nodiscard]] bool               loaded() const override { return loaded_; }
    [[nodiscard]] const std::string& path() const override { return path_; }
    [[nodiscard]] const std::string& problem() const override { return problem_; }
    [[nodiscard]] bool               dirty() const override { return stack_.dirty(); }
    [[nodiscard]] std::string        status() const override;
    [[nodiscard]] const char*        hint() const override;
    // Wider than the map's 260: this inspector carries sliders, and a slider in a
    // narrow column has too little travel to set a value with.
    [[nodiscard]] int inspector_width() const override { return 300; }

    void register_commands() override;

    void update(double dt, const platform::InputState& in, bool interactive) override;
    void draw_canvas(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) override;
    void draw_inspector(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) override;

    engine::OpResult save() override;
    engine::OpResult reload() override;
    std::optional<engine::OpResult> take_message() override;

    [[nodiscard]] bool recovery_pending() const override { return recovery_pending_; }
    void take_recovery() override;
    void dismiss_recovery() override;

    // ---- exposed for the host scene and for tests ---------------------------
    [[nodiscard]] const sandbox::World& world() const { return world_; }
    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] int  selected() const { return sel_; }   // index, -1 = nothing
    [[nodiscard]] std::size_t actor_count() const { return world_.alive(); }
    void toggle_play();
    // Textures are probed from the Texture Lab collection. Separate from the
    // constructor because it is I/O over ~40 speculative paths, and a test that only
    // exercises editing should not pay for it.
    void load_textures();
    // Where actor `index` lands on screen, in logical pixels — the one place the
    // fit-scale arithmetic lives, so the renderer, the hit test and a test cannot
    // disagree about it. Zero-width before the first draw, since only draw knows the
    // canvas. (Same reason MapWorkspace exposes tile_rect.)
    [[nodiscard]] ui::Rect actor_rect(int index) const;

private:
    // A palette entry = an Archetype plus which proto-carrying behaviour to attach.
    enum class Extra { None, Emitter, Sweeper };
    struct PaletteItem { const char* label; sandbox::Archetype arch; Extra extra = Extra::None; };

    void        load();
    void        note(bool ok, std::string msg);
    // Install `text` as the world without touching history — used by load, by undo's
    // closures, and by Stop. Every history-changing path goes through commit().
    void        install(const std::string& text);
    // Record the edit that just happened, as (scene before, scene after). A no-op
    // edit records nothing: history you did not make is history you cannot undo past.
    void        commit(const std::string& before, std::string label, std::uint64_t merge = 0);
    void        place(int palette_index, float wx, float wy);
    [[nodiscard]] int  index_at(float wx, float wy) const;   // topmost actor, -1 = none
    [[nodiscard]] bool entity_at(int index, ecs::Entity& out) const;

    std::string        path_, problem_;
    bool               loaded_ = false;
    sandbox::World     world_{};
    doc::CommandStack  stack_;
    std::vector<PaletteItem> palette_;

    bool        recovery_pending_ = false;
    std::string recovery_text_;

    int   armed_ = -1;        // palette index armed for placing; -1 = select/move
    int   sel_   = -1;        // selection by INDEX, not by handle: a snapshot restore
                              // builds new entities, and a stale handle would dangle
    bool  dragging_ = false;
    float drag_dx_ = 0, drag_dy_ = 0;
    std::string drag_before_;  // the scene as it was when the drag started
    int   color_idx_ = 0;

    bool        playing_ = false;
    std::string play_snapshot_;   // captured on Play, restored on Stop — NOT an edit

    // Canvas placement, remembered from the last draw: immediate mode has no layout
    // until it draws, so hit-testing uses the previous frame's geometry.
    ui::Rect canvas_{};
    float    view_scale_ = 1.0f;
    int      view_x_ = 0, view_y_ = 0;

    double autosave_timer_ = 0.0;
    double anim_time_ = 0.0;      // cosmetic flipbook clock; runs even when stopped
    bool   commands_registered_ = false;
    bool   textures_loaded_ = false;

    std::unordered_map<std::string, gfx::Image> tex_;
    std::vector<std::string>                    tex_names_;

    std::optional<engine::OpResult> message_;

    // Clicks resolve during draw and are acted on in the next update — the same
    // one-way street the map workspace and the Hub buttons use.
    int  want_palette_ = -2;      // -2 = nothing asked, -1 = select/move, >=0 = arm
    bool want_undo_ = false, want_redo_ = false, want_save_ = false;
    bool want_play_ = false, want_delete_ = false, want_recolor_ = false, want_tex_ = false;
    // A slider writes straight into the component while dragging; the edit is
    // committed once, on release, so one drag is one undo step.
    bool  editing_prop_ = false;
    std::string prop_before_;
};

} // namespace studioshell
