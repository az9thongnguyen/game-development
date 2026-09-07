// =============================================================================
//  tests/test_studio_layout.cpp  —  the Studio's divider, remembered
// =============================================================================
//  Two questions, and only one of them is about parsing.
//
//  The parsing half is the ordinary contract: a round trip, a version guard, a
//  refusal of nonsense, and forward-compatibility for a key this build has never
//  heard of.
//
//  The other half is `fit_inspector`, and it is the whole design of chapter 144:
//  the STORED width is never clamped and the DRAWN width always is. Both directions
//  are checked, because a clamp that never lifts is invisible from the side that
//  only ever shrinks the window — and it is the expensive failure: the layout you
//  spent time dragging is gone, silently, with the window still showing you
//  something perfectly reasonable.
// =============================================================================
#include "games/studio_shell/layout.hpp"

#include <cstdio>
#include <string>

#include "engine/assets.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

using studioshell::fit_inspector;
using studioshell::kMinInspector;
using studioshell::Layout;
using studioshell::parse_layout;
using studioshell::to_text;

static void test_round_trip() {
    Layout l;
    l.set("Map", 300);
    l.set("Scene", 420);
    const auto back = parse_layout(to_text(l));
    CHECK(back.has_value());
    CHECK(back->width_for("Map", 0) == 300);
    CHECK(back->width_for("Scene", 0) == 420);
    // A workspace the file says nothing about gets what it asked for, not a zero.
    CHECK(back->width_for("Pixels", 260) == 260);
    // Deterministic bytes: the map iterates sorted, so the same layout is the same file.
    CHECK(to_text(*back) == to_text(l));
}

static void test_refusals_and_forward_compatibility() {
    CHECK(!parse_layout("").has_value());
    CHECK(!parse_layout("layout 1\n").has_value());             // wrong magic
    CHECK(!parse_layout("studiolayout 0\n").has_value());       // before version 1
    // A file from the FUTURE is refused whole rather than half-read — the map2 rule.
    CHECK(!parse_layout("studiolayout 2\ninspector Map 300\n").has_value());
    CHECK(!parse_layout("studiolayout 1\ninspector Map 0\n").has_value());
    CHECK(!parse_layout("studiolayout 1\ninspector Map\n").has_value());
    CHECK(!parse_layout("studiolayout 1\ninspector Map -40\n").has_value());

    // ...but an unknown KEY at the current version is skipped, so a later field is
    // additive and today's build still reads tomorrow's file.
    const auto l = parse_layout("studiolayout 1\nsidebar 40\ninspector Map 300\n");
    CHECK(l.has_value());
    CHECK(l->width_for("Map", 0) == 300);
}

static void test_the_clamp_lifts() {
    // A comfortable window: what was stored is what is drawn.
    CHECK(fit_inspector(300, 1200) == 300);

    // A narrow one: the canvas never gets thinner than the panel beside it.
    CHECK(fit_inspector(300, 500) == 250);
    CHECK(fit_inspector(900, 500) == 250);

    // ...and the SAME stored value comes back in full when there is room again. This
    // is the direction the clamp exists to survive: fit_inspector is a function of the
    // window, so nothing about the narrow frame is remembered.
    CHECK(fit_inspector(900, 2400) == 900);

    // The floor, and the floor giving way. A panel below kMinInspector is a scroll bar
    // with ambitions — except in a window so small that half of it is already less.
    CHECK(fit_inspector(40, 1200) == kMinInspector);
    CHECK(fit_inspector(40, 200) == 100);        // half of 200 is under the floor
    CHECK(fit_inspector(0, 0) == 0);             // nothing to divide

    // What a first run gets: the workspace's own request, unclamped in a real window.
    Layout empty;
    CHECK(fit_inspector(empty.width_for("Map", 260), 1200) == 260);
}

static void test_it_survives_the_disk() {
    assets::set_base_path(SCRATCH_ROOT);
    Layout l;
    l.set("Mixer", 333);
    CHECK(studioshell::write_layout(l));
    const Layout back = studioshell::read_layout();
    CHECK(back.width_for("Mixer", 0) == 333);

    // A file that cannot be parsed is not an error anybody can act on: the answer is
    // the default layout, which is exactly what a first run gets.
    const std::string junk = "studiolayout 99\n";
    CHECK(assets::write_file(studioshell::kLayoutPath,
                             std::vector<std::uint8_t>(junk.begin(), junk.end())));
    CHECK(studioshell::read_layout().width_for("Mixer", 260) == 260);
}

int main() {
    test_round_trip();
    test_refusals_and_forward_compatibility();
    test_the_clamp_lifts();
    test_it_survives_the_disk();
    if (g_failures == 0) std::printf("studio_layout: all tests passed\n");
    else                 std::printf("studio_layout: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
