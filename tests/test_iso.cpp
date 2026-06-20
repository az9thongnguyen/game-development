// =============================================================================
//  tests/test_iso.cpp  —  iso projection · ECS · A* · Farm · serialize (no SDL)
// =============================================================================
#include "engine/iso.hpp"
#include "games/iso/ecs.hpp"
#include "games/iso/farm.hpp"
#include "games/iso/pathfind.hpp"
#include "games/iso/serialize.hpp"
#include "games/iso/tilemap.hpp"

#include <cmath>
#include <cstdio>

using namespace iso;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool approx(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(a) + std::fabs(b));
}

// ---- projection -------------------------------------------------------------
static void test_projection() {
    // grid → screen → grid round-trips to the same tile for several cells.
    for (int gy = 0; gy < 6; ++gy) {
        for (int gx = 0; gx < 6; ++gx) {
            const ScreenPt s = grid_to_screen(static_cast<float>(gx), static_cast<float>(gy), 400, 60);
            // sample slightly inside the diamond center to dodge edge rounding
            const Vec2i back = screen_to_grid(s.x, s.y, 400, 60);
            CHECK(back == (Vec2i{gx, gy}));
        }
    }
    // origin maps to the offset; +gx is down-right, +gy is down-left.
    const ScreenPt o = grid_to_screen(0, 0, 100, 50);
    CHECK(approx(o.x, 100) && approx(o.y, 50));
    const ScreenPt px = grid_to_screen(1, 0, 0, 0);
    CHECK(px.x > 0 && px.y > 0);              // down-right
    const ScreenPt py = grid_to_screen(0, 1, 0, 0);
    CHECK(py.x < 0 && py.y > 0);              // down-left
    // depth key grows toward the camera.
    CHECK(depth_key(2, 3) > depth_key(1, 1));
}

// ---- ECS --------------------------------------------------------------------
static void test_ecs() {
    World w;
    const Entity a = w.create(), b = w.create(), c = w.create();
    CHECK(a == 1 && b == 2 && c == 3);
    w.positions.add(a, {1, 1});
    w.positions.add(b, {2, 2});
    w.positions.add(c, {3, 3});
    CHECK(w.positions.size() == 3 && w.positions.has(b));
    CHECK(w.positions.get(b)->x == 2.0f);

    w.destroy(b);                              // swap-and-pop
    CHECK(!w.positions.has(b) && w.positions.size() == 2);
    CHECK(w.positions.get(a)->x == 1.0f && w.positions.get(c)->x == 3.0f);  // survivors
    CHECK(w.alive().size() == 2);

    w.positions.add(a, {9, 9});                // overwrite (no new slot)
    CHECK(w.positions.size() == 2 && w.positions.get(a)->x == 9.0f);

    w.clear();
    CHECK(w.positions.size() == 0 && w.alive().empty());
    CHECK(w.create() == 1);                    // ids restart after clear
}

// ---- tile map ---------------------------------------------------------------
static void test_tilemap() {
    TileMap m(5, 4, Terrain::Grass);
    CHECK(m.width() == 5 && m.height() == 4);
    CHECK(m.terrain_walkable(0, 0));
    m.set(1, 1, Terrain::Water);
    CHECK(!m.terrain_walkable(1, 1));
    CHECK(!m.in_bounds(5, 0) && !m.in_bounds(-1, 0));
    CHECK(m.at(99, 99) == Terrain::Grass);     // safe out-of-bounds sentinel
}

// ---- A* ---------------------------------------------------------------------
static void test_astar() {
    auto open = [](int, int) { return true; };
    auto p1 = astar(5, 5, {0, 0}, {4, 0}, open);
    CHECK(p1.size() == 5 && p1.front() == (Vec2i{0, 0}) && p1.back() == (Vec2i{4, 0}));

    auto p2 = astar(5, 5, {0, 0}, {4, 4}, open);   // pure diagonal
    CHECK(p2.size() == 5);
    for (std::size_t i = 0; i < p2.size(); ++i)
        CHECK(p2[i].x == static_cast<int>(i) && p2[i].y == static_cast<int>(i));

    CHECK(astar(5, 5, {2, 2}, {2, 2}, open).size() == 1);   // start == goal

    auto wall = [](int x, int) { return x != 3; };          // full column blocked
    CHECK(astar(5, 5, {0, 0}, {4, 4}, wall).empty());       // unreachable

    // no corner cutting: both orthogonal neighbors of the (0,0)->(1,1) diagonal
    // are blocked AND there's no other route → no path.
    auto corner = [](int x, int y) { return !((x == 1 && y == 0) || (x == 0 && y == 1)); };
    CHECK(astar(3, 3, {0, 0}, {2, 2}, corner).empty());

    auto detour = [](int x, int y) { return !(x == 2 && y < 4); };  // wall with a gap
    auto p3 = astar(5, 6, {0, 0}, {4, 0}, detour);
    CHECK(!p3.empty() && p3.back() == (Vec2i{4, 0}));
}

// ---- Farm -------------------------------------------------------------------
static void test_farm() {
    Farm f(8, 8);
    const Entity t = f.place_object(3, 3, ObjKind::Tree);
    CHECK(t != kInvalid && f.object_at(3, 3) == t && !f.walkable(3, 3));
    f.place_object(3, 3, ObjKind::Wheat);          // replace; one object per tile
    CHECK(f.walkable(3, 3));                        // wheat is passable
    f.remove_object(3, 3);
    CHECK(f.object_at(3, 3) == kInvalid && f.walkable(3, 3));
    f.set_terrain(2, 2, Terrain::Water);
    CHECK(!f.walkable(2, 2));

    f.spawn_farmer(0, 0);
    CHECK(f.farmer() != kInvalid && f.farmer_cell() == (Vec2i{0, 0}));
    CHECK(f.command_farmer(5, 0));                  // clear route east
    for (int i = 0; i < 1200 && !(f.farmer_cell() == Vec2i{5, 0}); ++i) f.update(1.0 / 60.0);
    CHECK(f.farmer_cell() == (Vec2i{5, 0}));        // arrived
    f.set_terrain(7, 7, Terrain::Water);
    CHECK(!f.command_farmer(7, 7));                 // target blocked → no command
}

// ---- serialize --------------------------------------------------------------
static void test_serialize() {
    Farm d(1, 1);
    d.reset_default();
    const auto b1 = save_farm(d);
    Farm e(1, 1);
    CHECK(load_farm(e, b1));
    const auto b2 = save_farm(e);
    CHECK(b1 == b2);                                // byte-faithful round-trip
    CHECK(e.width() == 16 && e.height() == 16);
    CHECK(e.farmer_cell() == d.farmer_cell());
    CHECK(e.terrain_at(0, 8) == Terrain::Path);

    // transactional: malformed input leaves the target untouched.
    Farm g(4, 4);
    g.place_object(1, 1, ObjKind::Rock);
    const auto before = save_farm(g);
    std::vector<uint8_t> junk = {'n', 'o', 'p', 'e'};
    CHECK(!load_farm(g, junk));
    CHECK(save_farm(g) == before);
}

int main() {
    test_projection();
    test_ecs();
    test_tilemap();
    test_astar();
    test_farm();
    test_serialize();
    if (g_failures == 0) std::printf("iso: all tests passed\n");
    else                 std::printf("iso: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
