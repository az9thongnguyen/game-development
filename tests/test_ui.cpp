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
    if (g_failures == 0) std::printf("ui: all tests passed\n");
    else                 std::printf("ui: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
