// =============================================================================
//  engine/ui/ui.hpp  —  a tiny immediate-mode GUI (drawn into the framebuffer)
// =============================================================================
//  Immediate mode: no retained widget tree. Each frame you CALL a widget and it both
//  draws itself and returns its interaction (`if (ui.button(r, "Spawn")) …`). State
//  lives in the caller, not the UI. The core is the hot/active/focused model:
//    hot     = the widget under the mouse this frame
//    active  = the widget the mouse pressed down on (held until release)
//    focused = the widget the keyboard is driving (moved with Tab)
//    click   = release while hot == active, OR activate while focused
//  Everything draws via Renderer2D (no SDL); the logic runs with a null renderer too,
//  which is how the unit tests exercise it headless.
//
//  This header knows nothing about the platform: keyboard input arrives as INTENTS
//  (activate, cancel, copy, paste…), not as keys. engine/ui/ui_input.hpp does the
//  translation. That is what keeps "copy is Cmd+C here and Ctrl+C there" in one place
//  and keeps every widget testable without SDL.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/color.hpp"

namespace gfx { class Renderer2D; }

namespace ui {

struct Rect  { int x = 0, y = 0, w = 0, h = 0; };

// What the UI needs from the keyboard, as intents rather than keys.
struct Keys {
    bool tab = false, tab_back = false;   // move focus forward / backward
    bool activate = false;                // Enter or Space on the focused control
    bool cancel = false;                  // Escape
    bool left = false, right = false, up = false, down = false;
    bool home = false, end = false;
    bool backspace = false, del = false;
    bool copy = false, cut = false, paste = false, select_all = false;
    bool shift = false;                   // held — extends a selection rather than moving it
};

struct Input {
    int  mx = 0, my = 0;
    bool down = false, pressed = false, released = false;
    int  wheel = 0;                       // wheel ticks this frame (+ = away from the user)
    Keys keys{};
    // Text COMMITTED this frame (UTF-8, NOT NUL-terminated). Borrowed for the frame.
    const char* text     = nullptr;
    std::size_t text_len = 0;
};

using Id = std::uint32_t;

class Context {
public:
    void begin(gfx::Renderer2D* r, const Input& in);   // r may be null (headless)
    void end();

    // ---- identity -----------------------------------------------------------
    // A widget's id is a hash of its label combined with the id stack. Two widgets
    // with the same label in the same scope still collide — push a scope around
    // each repeated group (a row index, a panel name) and they no longer do.
    void push_id(const char* s);
    void push_id(int i);
    void pop_id();

    // ---- explicit-rect widgets (the testable core) --------------------------
    // `primary` uses the accent fill (one hot-action per screen); `enabled=false`
    // draws a muted, non-interactive control (always returns false).
    bool button(Rect r, const char* label, bool primary = false, bool enabled = true);
    bool checkbox(Rect r, const char* label, bool& value);     // true if toggled
    bool slider(Rect r, const char* label, float& value, float lo, float hi);  // true if changed
    void label(int x, int y, const char* text, gfx::Color color = gfx::colors::white);

    // ---- layout helpers (advance a vertical cursor inside a panel) ----------
    void panel(Rect bg, const char* title = nullptr);
    bool button(const char* label, bool primary = false, bool enabled = true);
    bool checkbox(const char* label, bool& value);
    bool slider(const char* label, float& value, float lo, float hi);
    void label(const char* text);

    // True if the mouse is over any widget/panel this frame (so the game can ignore
    // a click that the UI consumed). Query it AFTER the widgets, BEFORE or after end().
    [[nodiscard]] bool hovering_ui() const { return hovering_; }

    // Which control the keyboard is driving (0 = none). Exposed for tests and for a
    // scene that wants to skip its own shortcuts while a text field has focus.
    [[nodiscard]] Id focused() const { return focused_; }
    void set_focus(Id id) { focused_ = id; }

private:
    Id   id_of(const char* s) const;      // hashes the label, mixed with the id stack
    bool point_in(Rect r) const;
    // Register `id` at this point in the declaration order and handle the shared
    // hover/press/focus bookkeeping. Returns true if the widget was activated.
    bool interact(Id id, Rect r, bool enabled);
    void focus_ring(Rect r, int radius) const;

    gfx::Renderer2D* r_ = nullptr;
    Input            in_{};
    Id               hot_    = 0;   // recomputed each frame
    Id               active_ = 0;   // persists across frames during a drag
    Id               focused_ = 0;  // persists across frames
    bool             hovering_ = false;

    Id              id_scope_ = 0;        // top of the id stack
    std::vector<Id> id_stack_;
    std::vector<Id> tab_order_;           // focusable ids, in declaration order

    int cx_ = 0, cy_ = 0, cw_ = 0;  // layout cursor (panel-relative)
};

} // namespace ui
