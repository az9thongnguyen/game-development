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

// What a piece of status MEANS. Widgets map this to theme colours, so a caller
// never picks a colour and the palette can change in one place.
enum class Tone { Neutral, Info, Success, Warning, Danger, Accent };

// The answer to a confirmation. Pending means the user has not decided yet, so the
// caller keeps asking each frame.
enum class Confirm { Pending, Yes, No };

// Layout axis and per-layout options. At namespace scope rather than nested in
// Context so `LayoutOpts o = {}` can be a default argument (a nested type's default
// member initializers are not usable until the enclosing class is complete).
enum class Axis { X, Y };
struct LayoutOpts { int gap = 8; int pad = 0; };

class Context {
public:
    // `r` may be null (headless). screen_w/h default to the renderer's size and must
    // be given explicitly when there is no renderer — full-screen overlays need to
    // know how big the screen is.
    void begin(gfx::Renderer2D* r, const Input& in, int screen_w = 0, int screen_h = 0);
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

    // ---- layout ------------------------------------------------------------
    // A one-pass, single-axis cursor. Immediate mode cannot measure children before
    // placing them, so there is no general flex here: slots are taken from the start
    // of the area, or from the far end (which is how a footer or a right-aligned
    // button group is placed without knowing what comes after it), and `cell` divides
    // the space that is left into n equal parts because n is known at the call.
    //
    // That is deliberately less than a real flexbox. It is also everything the Studio
    // needs, and it fits in one pass with no measurement phase and no allocation.
    void begin_layout(Rect area, Axis axis, LayoutOpts o = {});
    void end_layout();
    Rect slot(int size);              // fixed size along the axis, taken from the start
    Rect slot_end(int size);          // ...taken from the far end instead
    Rect slot_rest();                 // everything still untaken
    Rect cell(int n, int index);      // one of n equal divisions of what is left
    void skip(int px = 0);            // skip px along the axis (0 = the layout's own gap)
    [[nodiscard]] int remaining() const;   // space left along the axis

    // ---- layout helpers (advance a vertical cursor inside a panel) ----------
    void panel(Rect bg, const char* title = nullptr);
    bool button(const char* label, bool primary = false, bool enabled = true);
    bool checkbox(const char* label, bool& value);
    bool slider(const char* label, float& value, float lo, float hi);
    void label(const char* text);

    // ---- status and overlays ------------------------------------------------
    // A pill: tinted background, coloured text. Status is shown as colour AND a word
    // — colour alone excludes anyone who cannot separate the hues and forces a
    // legend; a word alone makes the eye read every row. Returns its width so a
    // caller can right-align it.
    int badge(int x, int y, const char* text, Tone tone, gfx::Color on = 0);

    // Attach a tooltip to a rect. It is DEFERRED to end(): immediate mode paints as
    // it goes, so a tooltip declared halfway through a frame would be painted over
    // by everything declared after it. Deferring only the overlays is the smallest
    // fix that works — ordinary widgets keep drawing immediately and there is no
    // command list to keep in step with the renderer.
    void tooltip(Rect anchor, const char* text);

    // A transient message, drawn last, centred near the bottom of the screen. The
    // caller owns the timer; call this while it should be visible.
    void toast(const char* msg, Tone tone = Tone::Success);

    // Everything declared after this is inert (drawn, but it cannot be hovered,
    // clicked or focused) until end(). Call it before the normal UI when a modal is
    // up, so the screen behind stays visible and stops responding — which is what
    // "modal" means.
    void begin_inert();

    // A modal confirmation over the whole screen. Draws the scrim and the card
    // itself, and its own controls are live even while everything else is inert, so
    // the usual shape is: begin_inert(); ...normal UI...; confirm(...).
    Confirm confirm(const char* id, const char* title, const char* body,
                    const char* yes_label, bool danger = false);

    // ---- more controls -------------------------------------------------------
    // Editable single-line text. Returns true when the value changed this frame.
    // Caret and selection are byte offsets that always land on UTF-8 boundaries —
    // stepping by byte would let a caret sit inside a multi-byte character and
    // Backspace would then produce mojibake rather than delete a letter.
    bool text_input(const char* id, Rect r, std::string& value, const char* placeholder = nullptr);

    // Clipboard access as a seam. The widget must not know about SDL — a scene wires
    // these to platform::clipboard_get/set, and a test wires them to a local string.
    void set_clipboard(std::function<std::string()> get,
                       std::function<void(const std::string&)> set);

    // A scrolling viewport. Content is clipped to `r` and offset by the wheel; the
    // caller draws inside using the returned origin. content_h is how tall the
    // content is, which the caller knows and the UI cannot.
    Rect begin_scroll(const char* id, Rect r, int content_h);
    void end_scroll();

    // A row of tabs. Returns the selected index (which may differ from `current`).
    int tabs(const char* id, Rect r, const char* const* labels, int count, int current);

    // A selectable row: label on the left, optional secondary text and badge right.
    bool list_item(Rect r, const char* label, bool selected,
                   const char* secondary = nullptr, const char* badge_text = nullptr,
                   Tone badge_tone = Tone::Neutral);

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
    void draw_overlays();

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

    // Layout stack. Fixed depth, no allocation: a Context is long-lived but this is
    // touched every frame, and eight levels of nesting is already more than any
    // screen here has. `head`/`tail` are offsets from the two ends of the axis.
    struct Layout { Rect area; Axis axis; int gap; int head; int tail; };
    static constexpr int kMaxLayoutDepth = 8;
    Layout layouts_[kMaxLayoutDepth]{};
    int    layout_depth_ = 0;

    int  screen_w_ = 0, screen_h_ = 0;
    bool inert_ = false;               // set by begin_inert(), cleared at end()

    // Overlays, replayed in declaration order at end(). Deliberately data, not
    // closures: the set is small and fixed, and data is easier to reason about than
    // captured state that outlives the frame it came from.
    struct Overlay { bool is_toast; Rect anchor; std::string text; Tone tone; };
    std::vector<Overlay> overlays_;

    // Editing state for the ONE field that has focus. Only one can, so this is a
    // single record rather than a map keyed by id.
    struct TextState { Id id = 0; std::size_t caret = 0, anchor = 0; int scroll = 0; };
    TextState text_{};
    std::function<std::string()>            clip_get_;
    std::function<void(const std::string&)> clip_set_;

    struct ScrollState { Id id; int offset; };
    std::vector<ScrollState> scrolls_;
    int scroll_depth_ = 0;
    Id  scroll_open_[4]{};
};

} // namespace ui
