// =============================================================================
//  tests/test_ui.cpp  —  immediate-mode GUI logic (headless: null renderer)
// =============================================================================
#include "engine/ui/ui.hpp"

#include <cmath>
#include <cstdio>

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

int main() {
    test_button_click();
    test_checkbox();
    test_slider();
    test_button_states();
    test_id_stack();
    test_focus();
    test_slider_keyboard();
    if (g_failures == 0) std::printf("ui: all tests passed\n");
    else                 std::printf("ui: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
