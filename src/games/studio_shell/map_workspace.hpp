// =============================================================================
//  games/studio_shell/map_workspace.hpp  —  the Studio's map editor
// =============================================================================
//  The consumer that S3 and S4a were built for. Until this existed, `map2` had no
//  authoring tool (Map Lab still wrote the older fpsmap1), the command registry had
//  no palette, and the undo stack had nobody pushing onto it — three cores that were
//  tested and unused, which is the "motion without connection" the strategy warns
//  about. This workspace closes all three: it edits a real map2 through
//  mapedit::, records every edit on a doc::CommandStack, saves and autosaves through
//  doc::, and registers its operations in cmd:: so the palette lists them.
//
//  No SDL: it reads a platform::InputState (a plain struct) and draws through
//  Renderer2D, so the whole thing runs in the headless golden test.
//
//  It was written WITHOUT a Workspace interface on purpose — an abstract base with one
//  implementation is a shape moulded around its only occupant. The interface arrived
//  with the second workspace (chapter 116), and this class now implements it.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine/document/command_stack.hpp"
#include "engine/release/ops.hpp"
#include "engine/tilemap/map_edit.hpp"
#include "engine/ui/ui.hpp"
#include "games/studio_shell/workspace.hpp"
#include "platform/input.hpp"

namespace gfx { class Renderer2D; }

namespace studioshell {

class MapWorkspace : public Workspace {
public:
    enum class Tool { Paint, Rect, Fill };

    // `map_path` is asset-relative and may be empty (the project declares no map).
    // Loading happens here so the shell can show WHY there is no canvas.
    explicit MapWorkspace(std::string map_path);
    ~MapWorkspace() override;

    // Bind this object's operations to command ids. Called by the shell; the
    // destructor unregisters them, because a handler that captured `this` and
    // outlives it is a dangling call the palette would happily make.
    void register_commands() override;

    [[nodiscard]] const char*        name() const override { return "Map"; }
    [[nodiscard]] bool               loaded() const override { return loaded_; }
    [[nodiscard]] const std::string& path() const override { return path_; }
    [[nodiscard]] const std::string& problem() const override { return problem_; }
    [[nodiscard]] bool               dirty() const override { return stack_.dirty(); }
    [[nodiscard]] const tilemap::Map& map() const { return map_; }

    // The tile under the cursor belongs in the status line, and only this workspace
    // knows there are tiles at all — which is precisely why status() lives here.
    [[nodiscard]] std::string status() const override;
    [[nodiscard]] const char* hint() const override;

    // An autosave newer than the file was found on open. The shell owns the screen's
    // one modal, so it asks the question and calls back here.
    [[nodiscard]] bool recovery_pending() const override { return recovery_pending_; }
    void take_recovery() override;
    // Decline: keep the saved file and LEAVE the autosave where it is, so declining
    // by reflex cannot destroy the work. A real save clears it; until then the offer
    // comes back next time the map is opened.
    void dismiss_recovery() override;

    // `interactive` is false while a modal is up: the workspace still draws, but a
    // click behind a dialog must not paint a tile.
    void update(double dt, const platform::InputState& in, bool interactive) override;
    void draw_canvas(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) override;
    void draw_inspector(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) override;

    // Where a tile lands on the canvas, in logical pixels — the one place the pan and
    // zoom arithmetic lives, so a status bar, a test and the renderer cannot disagree
    // about it. Zero-width before the first draw, since only draw knows the canvas.
    [[nodiscard]] ui::Rect tile_rect(int tx, int ty) const;
    // The tile under the pointer, or -1 when it is off the map.
    [[nodiscard]] int hover_x() const { return hover_x_; }
    [[nodiscard]] int hover_y() const { return hover_y_; }

    // The last thing that happened, for the shell's toast. Cleared when read.
    std::optional<engine::OpResult> take_message() override;

    engine::OpResult save() override;
    engine::OpResult reload() override;

private:
    void        load();
    void        register_undo_redo();
    std::string layer_name() const;
    void        note(bool ok, std::string msg);

    std::string        path_;
    std::string        problem_;        // why there is no map, in words
    bool               loaded_ = false;
    tilemap::Map       map_{};
    doc::CommandStack  stack_;
    mapedit::Stroke    stroke_;

    // Recovery text held until the user answers; applying it without asking is how
    // someone loses the version they deliberately saved.
    bool        recovery_pending_ = false;
    std::string recovery_text_;

    Tool         tool_ = Tool::Paint;
    int          layer_ = 0;
    std::int32_t brush_ = 1;
    int          zoom_ = 2;
    int          pan_x_ = 0, pan_y_ = 0;   // canvas pixels, top-left of the map
    bool         centred_ = false;         // the map has been centred once

    // The canvas rect, remembered from the last draw: immediate mode has no layout
    // until it draws, so hit-testing uses the previous frame's geometry.
    ui::Rect canvas_{};

    // The tile under the cursor, and the in-progress rectangle. -1 = off the map.
    int  hover_x_ = -1, hover_y_ = -1;
    bool rect_active_ = false;
    int  rect_x0_ = 0, rect_y0_ = 0;

    bool  panning_ = false;
    int   pan_from_x_ = 0, pan_from_y_ = 0;
    int   pan_origin_x_ = 0, pan_origin_y_ = 0;

    double autosave_timer_ = 0.0;
    bool   commands_registered_ = false;

    std::optional<engine::OpResult> message_;

    // Clicks resolve during draw (that is where the layout is known) and are acted on
    // in the next update — the same one-way street the Hub buttons use.
    int  want_layer_ = -1;
    int  want_tool_  = -1;
    int  want_brush_ = -1;
    bool want_undo_ = false, want_redo_ = false, want_save_ = false;
};

// The colour a tile id draws as. There is no tileset renderer yet (that arrives with
// the sprite tooling), so ids are shown as a stable, distinguishable palette rather
// than as art — which is honest about what the editor currently knows.
gfx::Color tile_color(std::int32_t id);

} // namespace studioshell
