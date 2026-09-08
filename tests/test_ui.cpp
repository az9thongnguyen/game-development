// =============================================================================
//  tests/test_ui.cpp  —  immediate-mode GUI logic (headless: null renderer)
// =============================================================================
#include "engine/ui/ui.hpp"
#include "engine/ui/touch_input.hpp"
#include "platform/input.hpp"

#include <cmath>
#include <cstdio>
#include <type_traits>
#include <vector>
#include <string>

static_assert(std::is_trivially_copyable_v<platform::InputState>);

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static ui::Input press(int x, int y)   { return ui::Input{x, y, true,  true,  false}; }
static ui::Input hold(int x, int y)    { return ui::Input{x, y, true,  false, false}; }
static ui::Input release(int x, int y) { return ui::Input{x, y, false, false, true}; }
static ui::Input idle(int x, int y)    { return ui::Input{x, y, false, false, false}; }

// Keyboard-only frames: the mouse is parked far away so nothing is hovered.
static ui::Input keys(const ui::Keys& k) {
    ui::Input in{-1000, -1000, false, false, false};
    in.keys = k;
    return in;
}
static ui::Input k_tab()      { ui::Keys k; k.tab = true;      return keys(k); }
static ui::Input k_tab_back() { ui::Keys k; k.tab_back = true; return keys(k); }
static ui::Input k_activate() { ui::Keys k; k.activate = true; return keys(k); }

// ---------------------------------------------------------------------------
//  Id stack: two widgets with the same label under different scopes must be two
//  widgets. Without it, "Delete" in row 3 and "Delete" in row 7 are one control
//  and clicking either drives whichever happened to draw last — the exact footgun
//  the old header documented as a caller's problem.
// ---------------------------------------------------------------------------
static void test_id_stack() {
    ui::Context ui;
    const ui::Rect a{0, 0, 100, 30};
    const ui::Rect b{0, 100, 100, 30};

    // Same label, different scopes, click the SECOND one.
    auto frame = [&](const ui::Input& in, bool& ca, bool& cb) {
        ui.begin(nullptr, in);
        ui.push_id(1); ca = ui.button(a, "Delete"); ui.pop_id();
        ui.push_id(2); cb = ui.button(b, "Delete"); ui.pop_id();
        ui.end();
    };
    bool ca = false, cb = false;
    frame(press(10, 110), ca, cb);
    frame(release(10, 110), ca, cb);
    CHECK(!ca);   // the first "Delete" must not fire
    CHECK(cb);    // the second one does

    // ...and clicking the first fires only the first.
    frame(press(10, 10), ca, cb);
    frame(release(10, 10), ca, cb);
    CHECK(ca);
    CHECK(!cb);

    // WITHOUT scopes the two share one id, and this is the concrete damage: the
    // press marks that id active from the second button, then on release the FIRST
    // button sees active_ == its id, finds the mouse is not over it, reports no
    // click and clears active_ — so by the time the real button runs there is
    // nothing left to release against. The click is swallowed entirely.
    auto unscoped = [&](const ui::Input& in, bool& x, bool& y) {
        ui.begin(nullptr, in);
        x = ui.button(a, "Same");
        y = ui.button(b, "Same");
        ui.end();
    };
    bool x = false, y = false;
    unscoped(press(10, 110), x, y);
    unscoped(release(10, 110), x, y);
    CHECK(!x);
    CHECK(!y);        // neither fires: this is what push_id exists to prevent

    // An unbalanced pop is ignored rather than corrupting the scope.
    ui.begin(nullptr, idle(0, 0));
    ui.pop_id(); ui.pop_id();
    ui.push_id("panel");
    ui.pop_id();
    ui.end();
}

// ---------------------------------------------------------------------------
//  Focus: Tab walks declaration order, Enter/Space activates, and focus cannot
//  survive on a control that stopped being declared.
// ---------------------------------------------------------------------------
static void test_focus() {
    ui::Context ui;
    const ui::Rect r1{0, 0, 100, 30};
    const ui::Rect r2{0, 40, 100, 30};
    const ui::Rect r3{0, 80, 100, 30};

    bool c1 = false, c2 = false, c3 = false;
    auto three = [&](const ui::Input& in) {
        ui.begin(nullptr, in);
        c1 = ui.button(r1, "one");
        c2 = ui.button(r2, "two");
        c3 = ui.button(r3, "three");
        ui.end();
    };

    // Nothing focused to begin with; the first Tab lands on the FIRST control.
    three(idle(-1000, -1000));
    CHECK(ui.focused() == 0);
    three(k_tab());
    const ui::Id f1 = ui.focused();
    CHECK(f1 != 0);

    // Enter activates whatever has focus — and only that one.
    three(k_activate());
    CHECK(c1 && !c2 && !c3);

    // Tab moves exactly one step per press, wrapping at the end.
    three(k_tab());
    three(k_activate());
    CHECK(!c1 && c2 && !c3);

    three(k_tab());
    three(k_activate());
    CHECK(!c1 && !c2 && c3);

    three(k_tab());                       // wraps back to the first
    three(k_activate());
    CHECK(c1 && !c2 && !c3);

    // Shift+Tab walks backwards.
    three(k_tab_back());
    three(k_activate());
    CHECK(!c1 && !c2 && c3);

    // Clicking a control also gives it the keyboard.
    three(press(10, 50));
    three(release(10, 50));
    three(k_activate());
    CHECK(!c1 && c2 && !c3);

    // A disabled control is skipped entirely: it never takes focus and Tab passes
    // over it. Declaring only #1 (disabled) and #3 leaves #3 the only stop.
    ui.begin(nullptr, k_tab());
    ui.button(r1, "one", false, /*enabled*/ false);
    c3 = ui.button(r3, "three");
    ui.end();
    ui.begin(nullptr, k_activate());
    ui.button(r1, "one", false, false);
    c3 = ui.button(r3, "three");
    ui.end();
    CHECK(c3);

    // Focus cannot survive on a control that is no longer declared — otherwise a
    // hidden panel keeps swallowing the keyboard.
    ui.begin(nullptr, idle(-1000, -1000));
    ui.button(r2, "two");
    ui.end();
    CHECK(ui.focused() == 0);
}

// ---------------------------------------------------------------------------
//  A focused slider is adjustable from the keyboard.
// ---------------------------------------------------------------------------
static void test_slider_keyboard() {
    ui::Context ui;
    const ui::Rect r{0, 0, 100, 10};
    float v = 0.5f;

    ui.begin(nullptr, press(50, 5));  ui.slider(r, "s", v, 0.0f, 1.0f); ui.end();
    ui.begin(nullptr, release(50, 5)); ui.slider(r, "s", v, 0.0f, 1.0f); ui.end();
    const float after_click = v;

    ui::Keys kr; kr.right = true;
    ui.begin(nullptr, keys(kr)); const bool ch = ui.slider(r, "s", v, 0.0f, 1.0f); ui.end();
    CHECK(ch);
    CHECK(v > after_click);
    CHECK(approx(v, after_click + 1.0f / 50.0f));

    ui::Keys kl; kl.left = true;
    ui.begin(nullptr, keys(kl)); ui.slider(r, "s", v, 0.0f, 1.0f); ui.end();
    CHECK(approx(v, after_click));

    // ...and it clamps at the ends rather than running past them.
    v = 1.0f;
    for (int i = 0; i < 5; ++i) { ui.begin(nullptr, keys(kr)); ui.slider(r, "s", v, 0.0f, 1.0f); ui.end(); }
    CHECK(approx(v, 1.0f));
    v = 0.0f;
    for (int i = 0; i < 5; ++i) { ui.begin(nullptr, keys(kl)); ui.slider(r, "s", v, 0.0f, 1.0f); ui.end(); }
    CHECK(approx(v, 0.0f));
}

static void test_button_click() {
    ui::Context ui;
    const ui::Rect r{0, 0, 100, 30};

    // press inside (no click yet), then release inside → click.
    ui.begin(nullptr, press(10, 10));   bool c1 = ui.button(r, "B"); ui.end();
    ui.begin(nullptr, release(10, 10)); bool c2 = ui.button(r, "B"); ui.end();
    CHECK(!c1 && c2);

    // press inside, release OUTSIDE → no click.
    ui.begin(nullptr, press(10, 10));     ui.button(r, "B"); ui.end();
    ui.begin(nullptr, release(500, 500)); bool c3 = ui.button(r, "B"); ui.end();
    CHECK(!c3);

    // hovering flag set when the mouse is over a widget.
    ui.begin(nullptr, idle(10, 10)); ui.button(r, "B"); ui.end();
    CHECK(ui.hovering_ui());
    ui.begin(nullptr, idle(500, 500)); ui.button(r, "B"); ui.end();
    CHECK(!ui.hovering_ui());
}

static void test_checkbox() {
    ui::Context ui;
    const ui::Rect r{0, 0, 20, 20};
    bool v = false;
    ui.begin(nullptr, press(5, 5));   ui.checkbox(r, "C", v); ui.end();
    ui.begin(nullptr, release(5, 5)); bool t = ui.checkbox(r, "C", v); ui.end();
    CHECK(t && v);                                  // toggled on
    ui.begin(nullptr, press(5, 5));   ui.checkbox(r, "C", v); ui.end();
    ui.begin(nullptr, release(5, 5)); ui.checkbox(r, "C", v); ui.end();
    CHECK(!v);                                       // toggled off

    // press inside, release OUTSIDE → no toggle.
    ui.begin(nullptr, press(5, 5));       ui.checkbox(r, "C", v); ui.end();
    ui.begin(nullptr, release(500, 500)); bool t2 = ui.checkbox(r, "C", v); ui.end();
    CHECK(!t2 && !v);
}

static void test_slider() {
    ui::Context ui;
    const ui::Rect r{0, 0, 100, 10};
    float val = 0.0f;

    // press at the middle → value ≈ midpoint of [0,10].
    ui.begin(nullptr, press(50, 5)); bool ch = ui.slider(r, "S", val, 0.0f, 10.0f); ui.end();
    CHECK(ch && approx(val, 5.0f));

    // drag to the far right → clamps to hi.
    ui.begin(nullptr, hold(100, 5)); ui.slider(r, "S", val, 0.0f, 10.0f); ui.end();
    CHECK(approx(val, 10.0f));

    // drag past the left edge → clamps to lo.
    ui.begin(nullptr, hold(-20, 5)); ui.slider(r, "S", val, 0.0f, 10.0f); ui.end();
    CHECK(approx(val, 0.0f));

    // release → no longer active; a hover (no drag) does not change the value.
    ui.begin(nullptr, release(50, 5)); ui.slider(r, "S", val, 0.0f, 10.0f); ui.end();
    const float before = val;
    ui.begin(nullptr, idle(70, 5)); bool ch2 = ui.slider(r, "S", val, 0.0f, 10.0f); ui.end();
    CHECK(!ch2 && approx(val, before));

    // degenerate zero-width track must not divide by zero / crash.
    const ui::Rect z{0, 0, 0, 10};
    float zv = 3.0f;
    ui.begin(nullptr, press(0, 5)); bool ch3 = ui.slider(z, "Z", zv, 0.0f, 10.0f); ui.end();
    CHECK(!ch3 && approx(zv, 3.0f));
}

static void test_button_states() {
    ui::Context ui;
    const ui::Rect r{0, 0, 100, 30};

    // Disabled button: press+release inside yields NO click and captures no hover.
    ui.begin(nullptr, press(10, 10));   ui.button(r, "D", /*primary*/false, /*enabled*/false); ui.end();
    ui.begin(nullptr, release(10, 10)); bool c = ui.button(r, "D", false, false); ui.end();
    CHECK(!c);
    ui.begin(nullptr, idle(10, 10)); ui.button(r, "D", false, false); ui.end();
    CHECK(!ui.hovering_ui());

    // Primary is a visual variant only — still fully clickable.
    ui.begin(nullptr, press(10, 10));   ui.button(r, "P", /*primary*/true); ui.end();
    ui.begin(nullptr, release(10, 10)); bool c2 = ui.button(r, "P", true); ui.end();
    CHECK(c2);
}

// ---------------------------------------------------------------------------
//  Layout: one pass, no measurement. Slots come off either end of the axis, and
//  `cell` divides what is LEFT so a row of equal columns can follow a header.
// ---------------------------------------------------------------------------
static bool same(ui::Rect a, ui::Rect b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static void test_layout() {
    ui::Context ui;
    ui.begin(nullptr, idle(-1000, -1000));

    // Column: slots stack downward, separated by the gap, and span the full width.
    ui.begin_layout(ui::Rect{10, 20, 200, 100}, ui::Axis::Y, ui::LayoutOpts{/*gap*/ 4, /*pad*/ 0});
    CHECK(ui.remaining() == 100);
    CHECK(same(ui.slot(30), ui::Rect{10, 20, 200, 30}));
    CHECK(ui.remaining() == 66);                       // 100 - 30 - 4
    CHECK(same(ui.slot(30), ui::Rect{10, 54, 200, 30}));
    // Taken from the far end instead: this is how a footer is placed without
    // knowing how much comes before it.
    CHECK(same(ui.slot_end(20), ui::Rect{10, 100, 200, 20}));
    CHECK(ui.remaining() == 8);                        // 100 - 30-4 - 30-4 - 20-4
    ui.end_layout();

    // Row: the same, along X.
    ui.begin_layout(ui::Rect{0, 0, 100, 40}, ui::Axis::X, ui::LayoutOpts{10, 0});
    CHECK(same(ui.slot(20), ui::Rect{0, 0, 20, 40}));
    CHECK(same(ui.slot(20), ui::Rect{30, 0, 20, 40}));
    ui.end_layout();

    // Padding shrinks the area on every side before anything is placed.
    ui.begin_layout(ui::Rect{0, 0, 100, 100}, ui::Axis::Y, ui::LayoutOpts{0, 10});
    CHECK(same(ui.slot(10), ui::Rect{10, 10, 80, 10}));
    ui.end_layout();

    // cell(n, i) divides what is LEFT — three columns after a fixed header row.
    ui.begin_layout(ui::Rect{0, 0, 100, 320}, ui::Axis::Y, ui::LayoutOpts{10, 0});
    ui.slot(100);                                       // header eats 100 + 10 gap
    CHECK(ui.remaining() == 210);
    // 210 = 3*size + 2*10  ->  size = 63
    CHECK(same(ui.cell(3, 0), ui::Rect{0, 110, 100, 63}));
    CHECK(same(ui.cell(3, 1), ui::Rect{0, 183, 100, 63}));
    CHECK(same(ui.cell(3, 2), ui::Rect{0, 256, 100, 63}));
    // cell does not advance the cursor — the caller asks for each index of one row.
    CHECK(ui.remaining() == 210);
    ui.end_layout();

    // A slot larger than what is left is clamped to the area rather than escaping it.
    ui.begin_layout(ui::Rect{0, 0, 50, 50}, ui::Axis::Y, ui::LayoutOpts{0, 0});
    const ui::Rect big = ui.slot(999);
    CHECK(big.h == 50);
    CHECK(ui.remaining() == 0);
    CHECK(same(ui.slot(10), ui::Rect{0, 50, 50, 0}));   // nothing left: zero-size
    ui.end_layout();

    // slot_rest takes everything untaken, from both ends.
    ui.begin_layout(ui::Rect{0, 0, 100, 100}, ui::Axis::Y, ui::LayoutOpts{0, 0});
    ui.slot(20);
    ui.slot_end(30);
    CHECK(same(ui.slot_rest(), ui::Rect{0, 20, 100, 50}));
    ui.end_layout();

    // Nesting: a row inside a column slot.
    ui.begin_layout(ui::Rect{0, 0, 100, 100}, ui::Axis::Y, ui::LayoutOpts{0, 0});
    const ui::Rect band = ui.slot(40);
    ui.begin_layout(band, ui::Axis::X, ui::LayoutOpts{0, 0});
    CHECK(same(ui.slot(25), ui::Rect{0, 0, 25, 40}));
    CHECK(same(ui.slot(25), ui::Rect{25, 0, 25, 40}));
    ui.end_layout();
    CHECK(same(ui.slot(10), ui::Rect{0, 40, 100, 10}));   // outer cursor is untouched
    ui.end_layout();

    // Unbalanced / absent layout: return an empty rect instead of reading garbage.
    CHECK(same(ui.slot(10), ui::Rect{}));
    ui.end_layout();
    ui.end_layout();
    CHECK(ui.remaining() == 0);

    ui.end();
}

// ---------------------------------------------------------------------------
//  Inert mode and the modal confirmation. "Modal" means the screen behind stays
//  visible and stops responding; without begin_inert() the scrim would be purely
//  decorative and clicks would still land on whatever is underneath it.
// ---------------------------------------------------------------------------
static void test_modal() {
    ui::Context ui;
    const ui::Rect behind{0, 0, 100, 30};

    // Baseline: the button behind works normally.
    bool hit = false;
    ui.begin(nullptr, press(10, 10), 800, 600);   hit = ui.button(behind, "behind"); ui.end();
    ui.begin(nullptr, release(10, 10), 800, 600); hit = ui.button(behind, "behind"); ui.end();
    CHECK(hit);

    // With begin_inert(), the same clicks do nothing to it.
    auto modal_frame = [&](const ui::Input& in, bool& behind_hit, ui::Confirm& res) {
        ui.begin(nullptr, in, 800, 600);
        ui.begin_inert();
        behind_hit = ui.button(behind, "behind");
        res = ui.confirm("del", "Delete?", "This cannot be undone.", "Delete", true);
        ui.end();
    };
    ui::Confirm res = ui::Confirm::Pending;
    modal_frame(press(10, 10), hit, res);
    modal_frame(release(10, 10), hit, res);
    CHECK(!hit);
    CHECK(res == ui::Confirm::Pending);

    // ...while the modal's own buttons stay live. At 800x600 the card is 420x170 at
    // (190,215); the confirm button is the rightmost, 120x30, 16px from the edges.
    const int bx = 190 + 420 - 16 - 120 + 60;    // centre of the confirm button
    const int by = 215 + 170 - 16 - 30 + 15;
    modal_frame(press(bx, by), hit, res);
    modal_frame(release(bx, by), hit, res);
    CHECK(res == ui::Confirm::Yes);
    CHECK(!hit);

    // Escape answers "no". A destructive action must not be reachable by a stray
    // Enter, so there is deliberately no keyboard shortcut for yes.
    ui::Keys esc; esc.cancel = true;
    modal_frame(keys(esc), hit, res);
    CHECK(res == ui::Confirm::No);

    // A freshly opened DESTRUCTIVE confirm focuses Cancel, not Delete: the first
    // Enter after a dialog appears is very often a reflex from whatever the user was
    // doing before it appeared. So Enter on a fresh destructive modal answers No.
    {
        ui::Context fresh;
        ui::Confirm r2 = ui::Confirm::Pending;
        bool h2 = false;
        fresh.begin(nullptr, idle(-1000, -1000), 800, 600);
        fresh.begin_inert();
        h2 = fresh.button(behind, "behind");
        r2 = fresh.confirm("del", "Delete?", "This cannot be undone.", "Delete", true);
        fresh.end();
        CHECK(r2 == ui::Confirm::Pending);

        fresh.begin(nullptr, k_activate(), 800, 600);
        fresh.begin_inert();
        h2 = fresh.button(behind, "behind");
        r2 = fresh.confirm("del", "Delete?", "This cannot be undone.", "Delete", true);
        fresh.end();
        CHECK(r2 == ui::Confirm::No);
        CHECK(!h2);
    }

    // A NON-destructive confirm defaults the other way: Enter accepts, which is what
    // makes "Save?" a one-keystroke dialog.
    {
        ui::Context fresh;
        ui::Confirm r2 = ui::Confirm::Pending;
        fresh.begin(nullptr, idle(-1000, -1000), 800, 600);
        r2 = fresh.confirm("save", "Save?", "Write the changes.", "Save", false);
        fresh.end();
        fresh.begin(nullptr, k_activate(), 800, 600);
        r2 = fresh.confirm("save", "Save?", "Write the changes.", "Save", false);
        fresh.end();
        CHECK(r2 == ui::Confirm::Yes);
    }

    // Focus is trapped inside the card: a control declared behind the modal cannot
    // take the keyboard, so Tab cannot walk out of the dialog.
    {
        ui::Context fresh;
        ui::Confirm r2 = ui::Confirm::Pending;
        for (int i = 0; i < 3; ++i) {
            fresh.begin(nullptr, k_tab(), 800, 600);
            fresh.begin_inert();
            fresh.button(behind, "behind");
            r2 = fresh.confirm("del", "Delete?", "body", "Delete", true);
            fresh.end();
        }
        // After three Tabs, Enter still answers the dialog rather than firing the
        // button behind it.
        bool behind_hit = false;
        fresh.begin(nullptr, k_activate(), 800, 600);
        fresh.begin_inert();
        behind_hit = fresh.button(behind, "behind");
        r2 = fresh.confirm("del", "Delete?", "body", "Delete", true);
        fresh.end();
        CHECK(!behind_hit);
        CHECK(r2 != ui::Confirm::Pending);
    }

    // inert_ is cleared by end(): the next frame is interactive again.
    ui.begin(nullptr, press(10, 10), 800, 600);   ui.button(behind, "behind"); ui.end();
    ui.begin(nullptr, release(10, 10), 800, 600); hit = ui.button(behind, "behind"); ui.end();
    CHECK(hit);
}

// ---------------------------------------------------------------------------
//  Overlays are declared during the frame and drawn at end(). Headless there is
//  nothing to draw, so what is checked here is that they neither crash nor leak
//  between frames, and that a tooltip only appears when its anchor is hovered.
// ---------------------------------------------------------------------------
static void test_overlays() {
    ui::Context ui;
    const ui::Rect anchor{0, 0, 50, 20};

    ui.begin(nullptr, idle(10, 10), 800, 600);
    ui.tooltip(anchor, "hovered");         // mouse is inside -> queued
    ui.toast("done", ui::Tone::Success);
    ui.end();

    ui.begin(nullptr, idle(500, 500), 800, 600);
    ui.tooltip(anchor, "not hovered");     // mouse is outside -> ignored
    ui.end();

    // An inert frame queues no tooltip: nothing behind a modal should respond,
    // including by explaining itself.
    ui.begin(nullptr, idle(10, 10), 800, 600);
    ui.begin_inert();
    ui.tooltip(anchor, "suppressed");
    ui.end();

    // Null text is ignored rather than dereferenced.
    ui.begin(nullptr, idle(10, 10), 800, 600);
    ui.tooltip(anchor, nullptr);
    ui.toast(nullptr);
    ui.end();
}

// ---------------------------------------------------------------------------
//  Text input. The caret and selection are byte offsets that must always land on
//  UTF-8 boundaries — stepping by byte would let Backspace chop a multi-byte
//  character in half and leave mojibake behind.
// ---------------------------------------------------------------------------
static ui::Input typed(const char* utf8) {
    ui::Input in{-1000, -1000, false, false, false};
    in.text = utf8;
    in.text_len = 0;
    for (const char* p = utf8; *p; ++p) ++in.text_len;
    return in;
}

static void test_text_input() {
    ui::Context ui;
    const ui::Rect r{0, 0, 200, 28};
    std::string v;

    // A fake clipboard, so the widget is testable with no SDL anywhere in sight.
    std::string board;
    ui.set_clipboard([&] { return board; }, [&](const std::string& s) { board = s; });

    auto frame = [&](const ui::Input& in) {
        ui.begin(nullptr, in, 400, 200);
        const bool ch = ui.text_input("name", r, v);
        ui.end();
        return ch;
    };

    // Click to focus, then type.
    frame(press(10, 10));
    frame(release(10, 10));
    CHECK(frame(typed("ab")));
    CHECK(v == "ab");

    // Backspace removes one character.
    ui::Keys bs; bs.backspace = true;
    frame(keys(bs));
    CHECK(v == "a");

    // A multi-byte character is ONE backspace, not three. This is the whole reason
    // the caret walks UTF-8 boundaries.
    frame(typed("→"));
    CHECK(v == "a→");
    frame(keys(bs));
    CHECK(v == "a");

    // ...and the same for a Vietnamese letter.
    frame(typed("ế"));
    CHECK(v == "aế");
    frame(keys(bs));
    CHECK(v == "a");

    // Left/right walk by character, not by byte: after typing a 3-byte character,
    // one Left puts the caret before it, so typing lands in front of it.
    v.clear();
    frame(typed("x→"));
    ui::Keys left; left.left = true;
    frame(keys(left));
    frame(typed("Z"));
    CHECK(v == "xZ→");

    // Select-all then type replaces everything.
    ui::Keys sa; sa.select_all = true;
    frame(keys(sa));
    frame(typed("new"));
    CHECK(v == "new");

    // Copy / paste round-trips through the seam.
    frame(keys(sa));
    ui::Keys cp; cp.copy = true;
    frame(keys(cp));
    CHECK(board == "new");
    ui::Keys end_k; end_k.end = true;
    frame(keys(end_k));
    ui::Keys pa; pa.paste = true;
    frame(keys(pa));
    CHECK(v == "newnew");

    // Cut removes the selection and puts it on the clipboard.
    frame(keys(sa));
    ui::Keys cut; cut.cut = true;
    frame(keys(cut));
    CHECK(v.empty());
    CHECK(board == "newnew");

    // Shift+Left extends a selection rather than moving; typing then replaces it.
    v = "abcd";
    frame(keys(end_k));
    ui::Keys shl; shl.left = true; shl.shift = true;
    frame(keys(shl));
    frame(keys(shl));
    frame(typed("Z"));
    CHECK(v == "abZ");

    // Delete removes forward; at the end it does nothing rather than misbehaving.
    v = "ab";
    ui::Keys home_k; home_k.home = true;
    frame(keys(home_k));
    ui::Keys del; del.del = true;
    frame(keys(del));
    CHECK(v == "b");
    frame(keys(end_k));
    frame(keys(del));
    CHECK(v == "b");
    frame(keys(bs));
    CHECK(v.empty());
    frame(keys(bs));        // backspace on an empty field: no crash, no change
    CHECK(v.empty());

    // An unfocused field ignores typing entirely.
    ui::Context other;
    std::string v2 = "kept";
    other.begin(nullptr, typed("XYZ"), 400, 200);
    other.text_input("f", r, v2);
    other.end();
    CHECK(v2 == "kept");
}

// ---------------------------------------------------------------------------
//  Scrolling viewport: the wheel moves the content and the offset is clamped to
//  what there actually is to scroll.
// ---------------------------------------------------------------------------
static void test_scroll() {
    ui::Context ui;
    const ui::Rect view{0, 0, 100, 100};

    auto scroll_by = [&](int ticks) {
        ui::Input in{10, 10, false, false, false};
        in.wheel = ticks;
        ui.begin(nullptr, in, 400, 400);
        const ui::Rect content = ui.begin_scroll("list", view, 300);
        ui.end_scroll();
        ui.end();
        return content.y;
    };

    CHECK(scroll_by(0) == 0);
    CHECK(scroll_by(-1) == -40);      // one tick down moves content up by 40
    CHECK(scroll_by(-1) == -80);
    for (int i = 0; i < 20; ++i) scroll_by(-1);
    CHECK(scroll_by(0) == -200);      // clamped: 300 content - 100 view
    for (int i = 0; i < 20; ++i) scroll_by(1);
    CHECK(scroll_by(0) == 0);         // clamped at the top too

    // Content that fits does not scroll at all.
    ui::Input in{10, 10, false, false, false};
    in.wheel = -5;
    ui.begin(nullptr, in, 400, 400);
    const ui::Rect c = ui.begin_scroll("small", view, 50);
    ui.end_scroll();
    ui.end();
    CHECK(c.y == 0);
}

// A widget scrolled out of a viewport must be DEAD, not merely invisible. Drawing is
// clipped by the renderer; hit-testing was not, so a row scrolled above the top kept a
// live rect wherever the offset put it — taking clicks meant for whatever is drawn
// there now. Both directions, because a clip that never lets anything through is the
// same bug wearing the opposite sign (chapter 133).
static void test_scroll_clips_hit_testing() {
    ui::Context ui;
    const ui::Rect view{0, 100, 200, 100};      // the viewport starts 100 px down

    // Press and release over (20, 50): ABOVE the viewport. A row placed there by a
    // scroll offset must not activate.
    auto press_release_at = [&](int mx, int my, int offset_ticks, int row_y) {
        // Scroll first, in its own frame — and with the pointer INSIDE the viewport,
        // because that is the only place a wheel tick counts. (Feeding the wheel at
        // the click point scrolled nothing and quietly made this test vacuous.)
        // Back to the top each time: the offset lives in the Context between frames,
        // so a second call would otherwise start where the first one stopped.
        for (int i = 0; i < 20; ++i) {
            ui::Input in{view.x + 5, view.y + 5, false, false, false};
            in.wheel = +1;
            ui.begin(nullptr, in, 400, 400);
            ui.begin_scroll("rows", view, 500);
            ui.end_scroll();
            ui.end();
        }
        for (int i = 0; i < -offset_ticks; ++i) {
            ui::Input in{view.x + 5, view.y + 5, false, false, false};
            in.wheel = -1;
            ui.begin(nullptr, in, 400, 400);
            ui.begin_scroll("rows", view, 500);
            ui.end_scroll();
            ui.end();
        }
        bool hit = false;
        for (int phase = 0; phase < 2; ++phase) {
            ui::Input in{mx, my, phase == 0, phase == 0, phase == 1};
            ui.begin(nullptr, in, 400, 400);
            const ui::Rect c = ui.begin_scroll("rows", view, 500);
            if (ui.button(ui::Rect{c.x, c.y + row_y, 200, 24}, "row")) hit = true;
            ui.end_scroll();
            ui.end();
        }
        return hit;
    };

    // Unscrolled, the row at body-y 0 sits at y=100..124 — inside the viewport, and
    // clickable there.
    CHECK(press_release_at(20, 110, 0, 0));
    // Scrolled down 4 ticks (160 px), the row at body-y 100 is drawn at y=40..64:
    // ON SCREEN but ABOVE the viewport, over whatever the caller drew there. A click
    // at 50 lands inside that rect and must do nothing.
    CHECK(!press_release_at(20, 50, -4, 100));
    // ...and the row that IS in view after the same scroll still works.
    CHECK(press_release_at(20, 110, -4, 170));
}

// ---------------------------------------------------------------------------
//  Tabs and list rows.
// ---------------------------------------------------------------------------
static void test_tabs_and_list() {
    ui::Context ui;
    const char* labels[] = {"One", "Two", "Three"};
    const ui::Rect bar{0, 0, 300, 30};
    int current = 0;

    auto click_tab = [&](int x) {
        ui.begin(nullptr, press(x, 15), 400, 400);
        ui.tabs("nav", bar, labels, 3, current);
        ui.end();
        ui.begin(nullptr, release(x, 15), 400, 400);
        current = ui.tabs("nav", bar, labels, 3, current);
        ui.end();
    };
    click_tab(150);   CHECK(current == 1);
    click_tab(250);   CHECK(current == 2);
    click_tab(50);    CHECK(current == 0);

    // Clicking the already-selected tab keeps it selected rather than toggling off.
    click_tab(50);    CHECK(current == 0);

    // A list row reports a click; two rows with the same label are distinguished by
    // the index scope around them, exactly as a real list would push.
    const ui::Rect r1{0, 100, 200, 24};
    const ui::Rect r2{0, 130, 200, 24};
    bool a = false, b = false;
    auto rows = [&](const ui::Input& in) {
        ui.begin(nullptr, in, 400, 400);
        ui.push_id(0); a = ui.list_item(r1, "item", false); ui.pop_id();
        ui.push_id(1); b = ui.list_item(r2, "item", true, "12 KB", "new", ui::Tone::Info); ui.pop_id();
        ui.end();
    };
    rows(press(10, 140));
    rows(release(10, 140));
    CHECK(!a);
    CHECK(b);
}

// ---------------------------------------------------------------------------
//  A confirmation that requires a reason. The release ops already write a reason
//  into the audit log; making the operator type one is what turns that log from a
//  list of timestamps into an account of why.
// ---------------------------------------------------------------------------
static void test_confirm_reason() {
    ui::Context ui;
    std::string reason;
    std::string board;
    ui.set_clipboard([&] { return board; }, [&](const std::string& s) { board = s; });

    ui::Confirm res = ui::Confirm::Pending;
    auto frame = [&](const ui::Input& in) {
        ui.begin(nullptr, in, 800, 600);
        ui.begin_inert();
        res = ui.confirm("promote", "Promote to production?",
                         "This changes what players get.", "Promote", false, &reason);
        ui.end();
    };

    // The card is 460x240 centred in 800x600 -> (170,180). Buttons sit 16px from the
    // bottom-right, 120x30 each.
    const int yes_x = 170 + 460 - 16 - 120 + 60;
    const int yes_y = 180 + 240 - 16 - 30 + 15;

    // With an empty reason the accept button is disabled: clicking it does nothing.
    frame(idle(-1000, -1000));
    frame(press(yes_x, yes_y));
    frame(release(yes_x, yes_y));
    CHECK(res == ui::Confirm::Pending);

    // The reason field has focus on open, so typing goes straight into it.
    frame(typed("shipping the fix"));
    CHECK(reason == "shipping the fix");

    // Now the accept button works.
    frame(press(yes_x, yes_y));
    frame(release(yes_x, yes_y));
    CHECK(res == ui::Confirm::Yes);

    // Cancel still works whatever the reason says.
    reason.clear();
    frame(idle(-1000, -1000));
    ui::Keys esc; esc.cancel = true;
    frame(keys(esc));
    CHECK(res == ui::Confirm::No);
}

// ---- the divider (chapter 144) ---------------------------------------------
// Everything here is a guard tested in BOTH directions: a limit that never binds and
// a limit that never lifts look identical from the side that only tries one of them.
static void test_splitter() {
    ui::Context ui;
    int pos = 100;
    const auto frame = [&](const ui::Input& in) {
        ui.begin(nullptr, in);
        const bool moved = ui.splitter("split", ui::Rect{pos, 0, 10, 200}, pos, 20, 180);
        ui.end();
        return moved;
    };

    // Hovering asks for the cursor and moves nothing.
    CHECK(!frame(idle(105, 50)));
    CHECK(pos == 100);
    CHECK(ui.cursor_hint() == ui::CursorHint::ResizeH);
    // ...and the hint is per FRAME: away from the handle it is gone again. A cursor
    // that only ever turns on is a cursor stuck as a resize arrow over the canvas.
    frame(idle(5, 5));
    CHECK(ui.cursor_hint() == ui::CursorHint::Default);

    // Pressing beside the handle does not grab it.
    frame(press(5, 50));
    frame(hold(60, 50));
    CHECK(pos == 100);

    // Grabbed 5px into the handle and dragged to 125 → 120, not 125. Without the
    // remembered offset the divider jumps by up to its own width before you move.
    frame(idle(-1, -1));
    frame(press(105, 50));
    CHECK(pos == 100);          // the press alone must not move it
    CHECK(frame(hold(125, 50)));
    CHECK(pos == 120);

    // A drag that lands where it already is is not a change.
    CHECK(!frame(hold(125, 50)));

    // Both ends of the clamp, and both are reached BY DRAGGING past them.
    frame(hold(9000, 50));
    CHECK(pos == 180);
    frame(hold(-9000, 50));
    CHECK(pos == 20);
    // ...and the clamp LIFTS: dragging back inside the range works again.
    frame(hold(305, 50));
    CHECK(pos == 180);          // 305 - the 5px grab offset = 300, still over the limit
    frame(hold(105, 50));
    CHECK(pos == 100);

    // Release ends the drag: moving the mouse afterwards moves nothing.
    frame(release(105, 50));
    frame(hold(300, 50));
    CHECK(pos == 100);

    // Inert (a modal is up): the handle is neither grabbable nor a cursor request.
    ui.begin(nullptr, press(105, 50));
    ui.begin_inert();
    ui.splitter("split", ui::Rect{pos, 0, 10, 200}, pos, 20, 180);
    ui.end();
    CHECK(pos == 100);
    CHECK(ui.cursor_hint() == ui::CursorHint::Default);
}

static void test_status_cells_join_one_way() {
    // The whole point of the type: a caller cannot punctuate it, and an empty cell is
    // not a separator with nothing on either side of it.
    const std::vector<ui::Seg> segs{{"level.map2"}, {""}, {"unsaved", ui::Tone::Warning},
                                    {"tile 3, 4"}};
    CHECK(ui::joined(segs) == "level.map2   unsaved   tile 3, 4");
    CHECK(ui::joined({}).empty());
    CHECK(ui::joined({{""}}).empty());
}

// ---- the two-axis pad (chapter 147) ----------------------------------------
static void test_xy_pad() {
    ui::Context ui;
    float x = 0.5f, y = 0.5f;
    const ui::Rect r{100, 100, 100, 100};
    const auto frame = [&](const ui::Input& in) {
        ui.begin(nullptr, in);
        const bool moved = ui.xy_pad("sv", r, x, y);
        ui.end();
        return moved;
    };

    // Hovering moves nothing.
    CHECK(!frame(idle(150, 150)));
    CHECK(x == 0.5f && y == 0.5f);

    // Pressing outside does not grab it, and a drag afterwards does nothing.
    frame(press(10, 10));
    frame(hold(150, 130));
    CHECK(x == 0.5f && y == 0.5f);

    // Grab it and drag. `y` runs DOWNWARD, like the framebuffer — a pad that reported
    // it upward would put the flip inside the widget, where a caller cannot see it.
    frame(idle(-1, -1));
    frame(press(150, 150));
    CHECK(frame(hold(125, 175)));
    CHECK(x == 0.25f);
    CHECK(y == 0.75f);

    // Both axes clamp, at both ends, and BOTH clamps lift again.
    frame(hold(-500, -500));
    CHECK(x == 0.0f && y == 0.0f);
    frame(hold(9000, 9000));
    CHECK(x == 1.0f && y == 1.0f);
    frame(hold(150, 150));
    CHECK(x == 0.5f && y == 0.5f);

    // A drag that lands where it already is is not a change.
    CHECK(!frame(hold(150, 150)));

    // A RELEASE is not a drag: letting go somewhere else in the same frame leaves the
    // value where the last held frame put it. A fast mouse does exactly this, and the
    // alternative — the release position counting — would let a sloppy hand nudge a
    // colour on the way up.
    frame(press(150, 150));
    frame(hold(150, 150));
    const float bx = x, by = y;
    frame(release(180, 190));
    CHECK(x == bx && y == by);

    // Release ends it: moving afterwards moves nothing.
    frame(release(150, 150));
    frame(hold(110, 110));
    CHECK(x == 0.5f && y == 0.5f);

    // ...and a press somewhere ELSE afterwards does not wake it up. `active_` is cleared
    // once a frame in end(); without that the last control dragged follows the next
    // press anywhere on the screen, which is the one thing that clear is for.
    frame(press(150, 150));
    frame(release(150, 150));
    const float ax = x, ay = y;
    frame(press(10, 10));
    frame(hold(120, 120));
    CHECK(x == ax && y == ay);

    // The keyboard reaches it, on both axes and in both directions — a control only a
    // mouse can use is what this project keeps having to go back and fix.
    frame(press(150, 150));
    frame(release(150, 150));
    const auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    ui::Keys k;      k.right = true;
    CHECK(frame(keys(k)));  CHECK(x > 0.5f);
    ui::Keys l;      l.left = true;
    CHECK(frame(keys(l)));  CHECK(near(x, 0.5f));
    ui::Keys dn;     dn.down = true;
    CHECK(frame(keys(dn))); CHECK(y > 0.5f);
    ui::Keys up;     up.up = true;
    CHECK(frame(keys(up))); CHECK(near(y, 0.5f));
    // ...and it stops at the edges rather than running off them.
    for (int i = 0; i < 200; ++i) frame(keys(l));
    CHECK(x == 0.0f);
    CHECK(!frame(keys(l)));      // already there: not a change
    for (int i = 0; i < 200; ++i) frame(keys(up));
    CHECK(y == 0.0f);

    // Inert (a modal is up): neither the pointer nor the keyboard reaches it. The pad
    // is FOCUSED first — a press that lands on nothing takes the keyboard back, and the
    // check above did exactly that, so without this the keyboard half of the assertion
    // is about a control that was not listening anyway.
    frame(press(150, 150));
    frame(release(150, 150));
    CHECK(ui.focused() == ui.id_for("sv"));
    // The key DOES move it, first — so the assertion below is about `inert_` and not
    // about a key nobody was listening for.
    CHECK(frame(keys(k)));
    const float kx = x, ky = y;
    // The KEYBOARD half before the pointer half, deliberately: a press that lands on
    // nothing takes the keyboard back, so testing the pointer first would leave the pad
    // unfocused and the keyboard check vacuous.
    ui.begin(nullptr, keys(k));
    ui.begin_inert();
    ui.xy_pad("sv", r, x, y);
    ui.end();
    CHECK(x == kx && y == ky);
    ui.begin(nullptr, press(120, 120));
    ui.begin_inert();
    ui.xy_pad("sv", r, x, y);
    ui.end();
    CHECK(x == kx && y == ky);
}

static void test_multi_touch_snapshot() {
    platform::InputState in;
    CHECK(in.device == platform::InputDevice::KeyboardMouse);

    in.begin_touch_frame();
    CHECK(in.begin_touch(41, 12, 34));
    CHECK(in.begin_touch(82, 210, 98));
    CHECK(in.device == platform::InputDevice::Touch);
    CHECK(in.touch_count() == 2);
    CHECK(in.touch(41) != nullptr && in.touch(41)->pressed && in.touch(41)->down);
    CHECK(in.touch(82) != nullptr && in.touch(82)->x == 210);

    in.begin_touch_frame();
    CHECK(!in.touch(41)->pressed && in.touch(41)->down);
    CHECK(in.move_touch(41, 30, 45));
    CHECK(in.touch(41)->x == 30 && in.touch(41)->y == 45);
    CHECK(in.end_touch(41, 31, 46));
    CHECK(!in.touch(41)->down && in.touch(41)->released);
    CHECK(in.touch(82)->down); // lifting one finger must not lift the other
    CHECK(in.touch_count() == 2); // a release edge is still part of this frame
    CHECK(!in.move_touch(41, 40, 50));
    CHECK(!in.end_touch(41, 40, 50));
    CHECK(!in.move_touch(999, 0, 0));

    in.begin_touch_frame();
    CHECK(in.touch(41) == nullptr); // released contacts live for exactly one frame
    CHECK(in.touch_count() == 1);
    CHECK(!in.end_touch(999, 0, 0));

    platform::InputState full;
    for (std::size_t i = 0; i < platform::InputState::kTouchMax; ++i)
        CHECK(full.begin_touch(static_cast<std::int64_t>(100 + i),
                               static_cast<int>(i), static_cast<int>(i)));
    CHECK(!full.begin_touch(999, 1, 1));
    CHECK(full.touch_count() == platform::InputState::kTouchMax);
}

static void test_semantic_button_options() {
    ui::Context ui;
    const ui::Rect r{0, 0, 120, 32};
    const ui::ButtonOptions danger{ui::ButtonKind::Danger, true,
                                   ui::Icon::Close, "Del"};
    ui.begin(nullptr, press(10, 10));
    CHECK(!ui.button(r, "Delete", danger));
    ui.end();
    ui.begin(nullptr, release(10, 10));
    CHECK(ui.button(r, "Delete", danger));
    ui.end();

    const ui::ButtonOptions disabled{ui::ButtonKind::Primary, false,
                                     ui::Icon::Play, nullptr};
    ui.begin(nullptr, press(10, 10));
    CHECK(!ui.button(r, "Run", disabled));
    ui.end();
    ui.begin(nullptr, release(10, 10));
    CHECK(!ui.button(r, "Run", disabled));
    ui.end();
}

static void test_touch_pointer_adapter() {
    platform::InputState in;
    in.mouse_x = 14;
    in.mouse_y = 29;
    in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;

    const touch::PointerSet mouse = touch::pointers(in);
    CHECK(mouse.count == 1);
    CHECK(mouse.primary().x == 14 && mouse.primary().y == 29);
    CHECK(mouse.primary().down && !mouse.primary().pressed);

    CHECK(in.begin_touch(7, 101, 102));
    CHECK(in.begin_touch(9, 201, 202));
    const touch::PointerSet fingers = touch::pointers(in);
    CHECK(fingers.count == 2);
    CHECK(fingers.data[0].x == 101 && fingers.data[0].pressed);
    CHECK(fingers.data[1].x == 201 && fingers.data[1].pressed);
    CHECK(fingers.down_in(touch::Box{96, 96, 20, 20}));
    CHECK(!fingers.down_in(touch::Box{300, 300, 20, 20}));
    CHECK(fingers.over(touch::Box{196, 196, 20, 20}));
    // Once real contacts exist, SDL's synthesized mouse must not become a third
    // action or make a control fire twice.
    CHECK(fingers.primary().x != in.mouse_x);

    CHECK(in.end_touch(7, 101, 102));
    const touch::PointerSet lifting = touch::pointers(in);
    CHECK(lifting.over(touch::Box{96, 96, 20, 20}));
    CHECK(!lifting.down_in(touch::Box{96, 96, 20, 20}));
}

int main() {
    test_button_click();
    test_checkbox();
    test_slider();
    test_button_states();
    test_id_stack();
    test_focus();
    test_slider_keyboard();
    test_layout();
    test_modal();
    test_overlays();
    test_text_input();
    test_scroll();
    test_scroll_clips_hit_testing();
    test_tabs_and_list();
    test_confirm_reason();
    test_splitter();
    test_status_cells_join_one_way();
    test_xy_pad();
    test_multi_touch_snapshot();
    test_touch_pointer_adapter();
    test_semantic_button_options();
    if (g_failures == 0) std::printf("ui: all tests passed\n");
    else                 std::printf("ui: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
