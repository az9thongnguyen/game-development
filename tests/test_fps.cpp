// =============================================================================
//  tests/test_fps.cpp  —  raycaster core unit tests (dependency-free, CTest)
// =============================================================================
//  Verifies the grid + the DDA ray cast: distances, which wall/side is hit, and
//  the fractional hit position — the math the whole FPS view depends on.
// =============================================================================
#include "engine/tilemap/map2.hpp"
#include "games/fps/map.hpp"
#include "games/fps/raycast.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif

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

// ---------------------------------------------------------------------------
//  fpsmap1 is gone from disk (chapter 132): `assets/maps/level_00.map2` replaced it,
//  Map Lab that wrote it is retired, and `fps::to_text`/`from_text` went with them.
//  What is left is ONE reader for old files, `tilemap::from_fpsmap1`, and the claim
//  that matters is that it changes nothing.
//
//  So the evidence stays even though the file does not: the exact bytes level_00 held
//  for 120 chapters, embedded here, must migrate to exactly the map2 that is
//  committed. That is a byte comparison — the same relationship `.hrt` has to its
//  `.recipe` — and it is a stronger check than the old one, which only compared two
//  readers of the same living file.
// ---------------------------------------------------------------------------
static const char kLegacyLevel00[] =
    "fpsmap1\n"
    "size 16 16\n"
    "row 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n"
    "row 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1\n"
    "row 1 0 0 0 0 0 0 0 2 0 2 2 2 2 0 1\n"
    "row 1 0 0 0 0 0 0 0 2 0 0 0 0 2 0 1\n"
    "row 1 0 0 0 0 0 0 0 2 0 2 0 0 2 0 1\n"
    "row 1 0 0 0 0 3 0 0 2 0 2 2 2 2 0 1\n"
    "row 1 0 0 0 0 0 0 0 2 0 0 0 0 0 0 1\n"
    "row 1 0 0 0 0 0 0 0 2 0 0 0 0 0 0 1\n"
    "row 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1\n"
    "row 1 0 0 0 0 0 0 0 2 0 0 0 0 0 0 1\n"
    "row 1 0 0 0 0 0 0 0 2 0 0 0 0 3 0 1\n"
    "row 1 0 0 0 0 3 0 0 2 0 0 0 0 0 0 1\n"
    "row 1 0 0 0 0 0 3 0 2 0 0 0 0 0 0 1\n"
    "row 1 0 0 0 0 0 0 0 2 0 0 0 0 0 0 1\n"
    "row 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1\n"
    "row 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n"
    "spawn 3 8 0.000000\n";

static void test_the_migration_changed_nothing() {
    auto migrated = tilemap::from_fpsmap1(kLegacyLevel00);
    CHECK(migrated.has_value());
    if (!migrated) return;

    std::ifstream f(std::string(ASSET_ROOT) + "/assets/maps/level_00.map2");
    CHECK(f.good());
    if (!f.good()) return;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string committed = ss.str();

    CHECK(tilemap::to_text(*migrated) == committed);
    if (tilemap::to_text(*migrated) != committed)
        std::printf("      maps/level_00.map2 is not what the old level migrates to:\n"
                    "      ./build/demo --cmd map.migrate <the old file> maps/level_00.map2\n");

    // ...and the GAME sees the same level either way, which is the thing a player
    // would notice. Grid for grid and spawn for spawn.
    auto legacy = from_shared_text(kLegacyLevel00);
    auto now    = from_shared_text(committed);
    CHECK(legacy.has_value() && now.has_value());
    if (legacy && now) {
        CHECK(now->w == legacy->w && now->h == legacy->h);
        CHECK(now->cells == legacy->cells);
        CHECK(now->spawn_cx == legacy->spawn_cx);
        CHECK(now->spawn_cy == legacy->spawn_cy);
        CHECK(now->spawn_dir == legacy->spawn_dir);
    }
}

static void test_shared_load() {
    // A hand-built level, so the check above does not depend on one asset happening
    // to be simple. It goes out through the shared writer now — the fpsmap1 writer
    // no longer exists, which is the point of this chapter.
    const Map d = default_map();
    tilemap::Map tm;
    tm.name = "d"; tm.w = d.w; tm.h = d.h; tm.tile = 16;
    tilemap::Layer wall;
    wall.name = "wall"; wall.kind = tilemap::LayerKind::Tiles;
    wall.cells.assign(d.cells.begin(), d.cells.end());
    tm.layers.push_back(std::move(wall));
    tilemap::Entity sp;
    sp.name = "spawn_player"; sp.x = d.spawn_cx; sp.y = d.spawn_cy;
    sp.props.push_back(tilemap::Property{"dir", std::to_string(d.spawn_dir)});
    tm.entities.push_back(std::move(sp));

    auto via_shared = from_shared_text(tilemap::to_text(tm));
    CHECK(via_shared.has_value());
    if (via_shared) {
        CHECK(via_shared->cells == d.cells);
        CHECK(via_shared->spawn_cx == d.spawn_cx);
        CHECK(via_shared->spawn_cy == d.spawn_cy);
    }

    auto native = from_shared_text(
        "map2 1\nname n\nsize 2 2\ntile 16\n"
        "layer wall tiles w\nrow 1 0\nrow 0 2\n"
        "entity spawn_player 1 0 dir=0\n");
    CHECK(native.has_value());
    if (native) {
        CHECK(native->at(0, 0) == 1);
        CHECK(native->at(1, 1) == 2);
        CHECK(native->spawn_cx == 1 && native->spawn_cy == 0);
    }

    CHECK(!from_shared_text("garbage").has_value());
    // A map2 with no spawn leaves the raycaster's own default in place rather than
    // dropping the player at 0,0 inside a wall.
    auto no_spawn = from_shared_text("map2 1\nname n\nsize 2 2\ntile 16\n"
                                     "layer wall tiles w\nrow 1 0\nrow 0 2\n");
    CHECK(no_spawn.has_value());
    if (no_spawn) CHECK(no_spawn->spawn_cx == -1);
}

int main() {
    test_the_migration_changed_nothing();
    test_shared_load();
    test_map();
    test_cast_east();
    test_cast_south();
    test_no_fisheye();
    test_project_sprite();
    test_perp_floor();

    if (g_failures == 0) std::printf("fps: all tests passed\n");
    else                 std::printf("fps: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
