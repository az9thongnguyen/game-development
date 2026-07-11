// =============================================================================
//  engine/ui/ui.hpp  —  a tiny immediate-mode GUI (drawn into the framebuffer)
// =============================================================================
//  Immediate mode: no retained widget tree. Each frame you CALL a widget and it both
//  draws itself and returns its interaction (`if (ui.button(r, "Spawn")) …`). State
//  lives in the caller, not the UI. The core is the hot/active model:
//    hot    = the widget under the mouse this frame
//    active = the widget the mouse pressed down on (held until release)
//    click  = release while hot == active
//  Everything draws via Renderer2D (no SDL); the logic runs with a null renderer too,
//  which is how the unit tests exercise it headless.
// =============================================================================
#pragma once

#include <cstdint>

#include "engine/color.hpp"

namespace gfx { class Renderer2D; }

namespace ui {

struct Rect  { int x = 0, y = 0, w = 0, h = 0; };
struct Input { int mx = 0, my = 0; bool down = false, pressed = false, released = false; };

class Context {
public:
    void begin(gfx::Renderer2D* r, const Input& in);   // r may be null (headless)
    void end();

    // ---- explicit-rect widgets (the testable core) ----
    // `primary` uses the accent fill (one hot-action per screen); `enabled=false`
    // draws a muted, non-interactive control (always returns false).
    bool button(Rect r, const char* label, bool primary = false, bool enabled = true);
    bool checkbox(Rect r, const char* label, bool& value);     // true if toggled
    bool slider(Rect r, const char* label, float& value, float lo, float hi);  // true if changed
    void label(int x, int y, const char* text, gfx::Color color = gfx::colors::white);

    // ---- layout helpers (advance a vertical cursor inside a panel) ----
    void panel(Rect bg, const char* title = nullptr);
    bool button(const char* label, bool primary = false, bool enabled = true);
    bool checkbox(const char* label, bool& value);
    bool slider(const char* label, float& value, float lo, float hi);
    void label(const char* text);

    // True if the mouse is over any widget/panel this frame (so the game can ignore
    // a click that the UI consumed). Query it AFTER the widgets, BEFORE or after end().
    [[nodiscard]] bool hovering_ui() const { return hovering_; }

    // CONTRACT: each widget's label must be UNIQUE within a begin()/end() frame — the
    // widget id is a hash of the label, so two same-labelled widgets share an id and
    // their interactions collide (a click can be swallowed). Real ImGui solves this
    // with an id stack / "##suffix"; for this engine, just keep labels distinct.

private:
    static std::uint32_t id_of(const char* s);
    bool point_in(Rect r) const;

    gfx::Renderer2D* r_ = nullptr;
    Input            in_{};
    std::uint32_t    hot_    = 0;   // recomputed each frame
    std::uint32_t    active_ = 0;   // persists across frames during a drag
    bool             hovering_ = false;

    int cx_ = 0, cy_ = 0, cw_ = 0;  // layout cursor (panel-relative)
};

} // namespace ui
