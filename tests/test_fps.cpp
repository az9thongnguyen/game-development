// =============================================================================
//  tests/test_fps.cpp  —  raycaster core unit tests (dependency-free, CTest)
// =============================================================================
//  Verifies the grid + the DDA ray cast: distances, which wall/side is hit, and
//  the fractional hit position — the math the whole FPS view depends on.
// =============================================================================
#include "games/fps/map.hpp"
#include "games/fps/raycast.hpp"

#include <cmath>
#include <cstdio>

using namespace fps;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool approx(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps * (1.0 + std::fabs(a) + std::fabs(b));
}

static void test_map() {
    const Map m = default_map();
    CHECK(m.w == 16 && m.h == 16);
    CHECK(m.at(0, 0) == 1);          // border
    CHECK(m.at(-1, 5) == 1);         // out of bounds = wall
    CHECK(m.at(16, 0) == 1);
    CHECK(m.at(5, 8) == 2);          // room wall
    CHECK(m.at(2, 2) == 3);          // pillar
    CHECK(m.at(7, 5) == 0);          // doorway is open
    CHECK(m.at(8, 8) == 0);          // inside the room is empty
}

static void test_cast_east() {
    // From (3.5, 8.5) looking +x, the first wall is the room's west wall at x=5,
    // cell (5,8) id 2, at distance 1.5; hit the centre of the cell face.
    const Map m = default_map();
    const Hit h = cast_ray(m, 3.5, 8.5, 1.0, 0.0);
    CHECK(approx(h.perp_dist, 1.5));
    CHECK(h.side == 0);
    CHECK(h.wall == 2);
    CHECK(h.map_x == 5 && h.map_y == 8);
    CHECK(approx(h.wall_x, 0.5));
}

static void test_cast_south() {
    // From (3.5, 8.5) looking +y, nothing is in column x=3 until the border at
    // y=15: distance 6.5, a y-side hit on wall id 1.
    const Map m = default_map();
    const Hit h = cast_ray(m, 3.5, 8.5, 0.0, 1.0);
    CHECK(approx(h.perp_dist, 6.5));
    CHECK(h.side == 1);
    CHECK(h.wall == 1);
    CHECK(h.map_y == 15);
}

static void test_no_fisheye() {
    // Two symmetric rays a little left/right of straight-ahead toward a flat wall
    // must give the SAME perpendicular distance (that's what kills fisheye).
    const Map m = default_map();
    const Hit a = cast_ray(m, 3.5, 8.5, 1.0,  0.10);
    const Hit b = cast_ray(m, 3.5, 8.5, 1.0, -0.10);
    CHECK(a.wall == 2 && b.wall == 2);
    CHECK(approx(a.perp_dist, b.perp_dist, 1e-9));
}

static void test_project_sprite() {
    // dir (1,0), plane (0,0.66). A sprite 2 units straight ahead is centred
    // (tx == 0) at depth 2.
    const Cam2 a = project_sprite(1, 0, 0, 0.66, 2.0, 0.0);
    CHECK(approx(a.tx, 0.0));
    CHECK(approx(a.ty, 2.0));
    // Off to the right -> tx > 0, same depth.
    const Cam2 b = project_sprite(1, 0, 0, 0.66, 2.0, 1.0);
    CHECK(b.tx > 0.0);
    CHECK(approx(b.ty, 2.0));
    // Behind the camera -> depth < 0.
    const Cam2 c = project_sprite(1, 0, 0, 0.66, -2.0, 0.0);
    CHECK(c.ty < 0.0);
}

static void test_perp_floor() {
    // Standing a hair from the wall at x=5: perp distance must be floored (never
    // tiny), so H/perp_dist can't overflow int when rendered.
    const Map m = default_map();
    const Hit h = cast_ray(m, 4.9995, 8.5, 1.0, 0.0);
    CHECK(h.perp_dist >= 0.001 - 1e-12);
    CHECK(h.wall == 2);
}

static void test_map_serialize() {
    // round-trip a hand-built grid through the shared fpsmap1 text form
    Map m; m.w = 3; m.h = 2; m.cells = {1, 1, 1, 1, 0, 2};
    const std::string s = to_text(m);
    auto r = from_text(s);
    CHECK(r && r->w == 3 && r->h == 2 && r->cells == m.cells);
    CHECK(to_text(*r) == s);                                  // stable round-trip
    // fail closed on malformed input
    CHECK(!from_text("garbage"));                             // bad header
    CHECK(!from_text("fpsmap1\nsize 3 2\nrow 1 1 1\n"));      // too few rows
    CHECK(!from_text("fpsmap1\nsize 3 2\nrow 1 1\nrow 1 1\n")); // short row
    CHECK(!from_text("fpsmap1\nsize 0 0\n"));                 // empty grid

    // spawn: unset by default, round-trips when set, and old (spawn-less) files parse.
    CHECK(r->spawn_cx == -1);                                 // no spawn line → unset
    Map ms = m; ms.spawn_cx = 2; ms.spawn_cy = 1; ms.spawn_dir = 1.5f;
    const std::string ss = to_text(ms);
    CHECK(ss.find("spawn 2 1") != std::string::npos);         // token emitted
    auto rs = from_text(ss);
    CHECK(rs && rs->spawn_cx == 2 && rs->spawn_cy == 1 && rs->spawn_dir == 1.5f);
    CHECK(to_text(*rs) == ss);                                // stable round-trip with spawn
    // an out-of-range spawn is rejected (grid stays valid, spawn stays unset)
    auto rbad = from_text("fpsmap1\nsize 3 2\nrow 1 1 1\nrow 1 0 2\nspawn 9 9 0\n");
    CHECK(rbad && rbad->spawn_cx == -1);
}

int main() {
    test_map();
    test_cast_east();
    test_cast_south();
    test_no_fisheye();
    test_project_sprite();
    test_perp_floor();
    test_map_serialize();

    if (g_failures == 0) std::printf("fps: all tests passed\n");
    else                 std::printf("fps: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
