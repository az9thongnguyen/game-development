// =============================================================================
//  tests/test_tilemap.cpp  —  map2 format, fpsmap1 migration, camera, autotile
// =============================================================================
//  Everything here is pure, so none of it needs a window, a renderer or a file. The
//  one thing that touches the repo is the migration check, which reads the real
//  authored level through ASSET_ROOT so the test cannot pass against a fixture that
//  has drifted from what --fps actually loads.
// =============================================================================
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "engine/tilemap/autotile.hpp"
#include "engine/tilemap/camera2d.hpp"
#include "engine/tilemap/map2.hpp"

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

using namespace tilemap;

static bool approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// ---------------------------------------------------------------------------
//  Format: round-trip, queries, and refusing what it cannot represent.
// ---------------------------------------------------------------------------
static void test_format() {
    const std::string text =
        "map2 1\n"
        "name farm_home\n"
        "size 4 3\n"
        "tile 16\n"
        "tileset ground tilesets/farm_ground.tsdef\n"
        "layer ground tiles ground\n"
        "row 1 2 3 4\n"
        "row 5 6 7 8\n"
        "row 9 10 11 12\n"
        "layer collide mask\n"
        "row 0 0 0 1\n"
        "row 0 1 0 0\n"
        "row 0 0 0 0\n"
        "entity spawn_player 1 1 dir=0.000000\n"
        "entity npc_anna 2 0 sched=anna.sched dialog=anna.dlg\n"
        "trigger door_house 3 0 1 2 target=house.map2 at=3,9\n";

    auto m = load(text);
    CHECK(m.has_value());
    if (!m) return;

    CHECK(m->name == "farm_home");
    CHECK(m->w == 4 && m->h == 3 && m->tile == 16);
    CHECK(m->tilesets.size() == 1 && m->tilesets[0].name == "ground");
    CHECK(m->layers.size() == 2);

    // Round-trip is exact: what was read writes back byte for byte. Anything less
    // means opening a map in an editor and saving it produces a spurious diff.
    CHECK(to_text(*m) == text);

    // A tiles layer with NO tileset must survive the same round trip. It used to
    // serialize to `layer ground tiles` with the field simply missing, and this very
    // parser then read the following "row" as the tileset name — so a map built in
    // memory (which is what an editor produces before any art exists) wrote a file
    // that would not open. The empty tileset is spelled "-".
    {
        Map bare;
        bare.name = "bare"; bare.w = 2; bare.h = 2; bare.tile = 8;
        Layer l; l.name = "ground"; l.kind = LayerKind::Tiles;
        l.cells = {1, 0, 0, 2};
        bare.layers.push_back(std::move(l));
        const std::string t = to_text(bare);
        CHECK(t.find("tiles -") != std::string::npos);
        const auto back = load(t);
        CHECK(back.has_value());
        CHECK(back && back->layers.size() == 1);
        CHECK(back && back->layers[0].tileset.empty());
        CHECK(back && back->at("ground", 1, 1) == 2);
        CHECK(back && to_text(*back) == t);
    }

    // Layer queries, including out of bounds and unknown layers.
    CHECK(m->at("ground", 0, 0) == 1);
    CHECK(m->at("ground", 3, 2) == 12);
    CHECK(m->at("ground", -1, 0) == 0);
    CHECK(m->at("ground", 4, 0) == 0);
    CHECK(m->at("nosuch", 0, 0) == 0);

    m->set("ground", 0, 0, 99);
    CHECK(m->at("ground", 0, 0) == 99);
    m->set("ground", -5, 0, 7);          // out of bounds: ignored, not a crash
    m->set("nosuch", 0, 0, 7);
    CHECK(m->at("ground", 0, 0) == 99);

    // Collision, including the rule that outside the world is solid.
    CHECK(m->solid(3, 0));
    CHECK(m->solid(1, 1));
    CHECK(!m->solid(0, 0));
    CHECK(m->solid(-1, 0));
    CHECK(m->solid(0, 99));

    // Entities and their properties.
    const Entity* anna = m->entity("npc_anna");
    CHECK(anna != nullptr);
    if (anna) {
        CHECK(anna->x == 2 && anna->y == 0);
        CHECK(prop(anna->props, "sched") == "anna.sched");
        CHECK(prop(anna->props, "dialog") == "anna.dlg");
        CHECK(prop(anna->props, "missing", "fallback") == "fallback");
    }
    CHECK(m->entity("nobody") == nullptr);

    // Triggers overlap by rect, not by corner.
    CHECK(m->triggers_at(3, 0, 1, 1).size() == 1);
    CHECK(m->triggers_at(3, 1, 1, 1).size() == 1);   // the trigger is 1x2
    CHECK(m->triggers_at(3, 2, 1, 1).empty());
    CHECK(m->triggers_at(0, 0, 4, 3).size() == 1);   // a query covering everything
    CHECK(m->triggers_at(0, 0, 1, 1).empty());

    // A map with no collision layer blocks nothing (but still walls the outside).
    auto plain = load("map2 1\nname p\nsize 2 2\ntile 8\nlayer g tiles t\nrow 0 0\nrow 0 0\n");
    CHECK(plain.has_value());
    if (plain) {
        CHECK(!plain->solid(0, 0));
        CHECK(plain->solid(-1, -1));
    }
}

static void test_format_rejects() {
    // A version from the future is refused rather than half-read: parsing a newer
    // schema with an older parser is how a tool silently drops fields it does not
    // know about and then writes the loss back to disk.
    CHECK(!load("map2 2\nname x\nsize 1 1\ntile 8\n").has_value());
    CHECK(!load("map2 0\nname x\nsize 1 1\ntile 8\n").has_value());

    CHECK(!load("").has_value());
    CHECK(!load("notamap\n").has_value());
    CHECK(!load("map2 1\nname x\ntile 8\n").has_value());                  // no size
    CHECK(!load("map2 1\nsize 0 4\ntile 8\n").has_value());                // zero size
    CHECK(!load("map2 1\nsize 2 2\nlayer g tiles t\nrow 1 2\n").has_value());   // short rows
    CHECK(!load("map2 1\nsize 2 1\nlayer g bogus\nrow 1 2\n").has_value());     // bad kind
    CHECK(!load("map2 1\nsize 2 1\nwibble 3\n").has_value());              // unknown directive
    // Rows before a size are unreadable, so the layer must be refused rather than
    // guessed at.
    CHECK(!load("map2 1\nlayer g tiles t\nrow 1\nsize 1 1\n").has_value());
}

// ---------------------------------------------------------------------------
//  Migration. fpsmap1 packs two facts into one number — the tile's appearance and
//  whether it is solid — so the migration has to split them.
// ---------------------------------------------------------------------------
static void test_migration() {
    const std::string old =
        "fpsmap1\n"
        "size 3 2\n"
        "row 1 0 2\n"
        "row 0 3 0\n"
        "spawn 1 1 1.570796\n";

    // load() sniffs the magic, so no caller has to know which era a file came from.
    auto m = load(old);
    CHECK(m.has_value());
    if (!m) return;

    CHECK(m->w == 3 && m->h == 2);
    CHECK(m->layers.size() == 2);

    // Appearance is preserved verbatim...
    CHECK(m->at("wall", 0, 0) == 1);
    CHECK(m->at("wall", 2, 0) == 2);
    CHECK(m->at("wall", 1, 1) == 3);
    CHECK(m->at("wall", 1, 0) == 0);

    // ...and solidity becomes the collide mask every other system reads.
    CHECK(m->solid(0, 0));
    CHECK(m->solid(2, 0));
    CHECK(m->solid(1, 1));
    CHECK(!m->solid(1, 0));
    CHECK(!m->solid(0, 1));

    // The spawn line becomes an entity, like every other authored point in map2,
    // with its facing kept so a migrated level starts exactly as it did.
    const Entity* sp = m->entity("spawn_player");
    CHECK(sp != nullptr);
    if (sp) {
        CHECK(sp->x == 1 && sp->y == 1);
        CHECK(approx(std::stof(prop(sp->props, "dir")), 1.570796f));
    }

    // A migrated map is a map2 map: it writes out in the new format and reads back
    // identically. That is what makes the migration one-way and final.
    const std::string round = to_text(*m);
    CHECK(round.rfind("map2 1\n", 0) == 0);
    auto again = load(round);
    CHECK(again.has_value());
    if (again) CHECK(to_text(*again) == round);

    // No spawn line is legal (older Lab files predate it) and produces no entity.
    auto nospawn = load("fpsmap1\nsize 1 1\nrow 0\n");
    CHECK(nospawn.has_value());
    if (nospawn) CHECK(nospawn->entities.empty());

    // Malformed old files are refused, not partially migrated.
    CHECK(!load("fpsmap1\nsize 2 2\nrow 1 1\n").has_value());
    CHECK(!load("fpsmap1\nsize 1 1\nrow 999\n").has_value());
}

// The REAL authored level, read from the repo — so this cannot pass against a
// fixture that has drifted away from what --fps actually loads.
static void test_migrate_real_level() {
    std::ifstream f(std::string(ASSET_ROOT) + "/assets/maps/level_00.map");
    CHECK(f.good());
    if (!f.good()) return;
    std::stringstream ss;
    ss << f.rdbuf();

    auto m = load(ss.str());
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->w == 16 && m->h == 16);
    CHECK(m->layer("wall") != nullptr);
    CHECK(m->layer("collide") != nullptr);
    CHECK(m->solid(0, 0));                       // the authored level has a border
    CHECK(m->entity("spawn_player") != nullptr); // ...and an authored start
}

// ---------------------------------------------------------------------------
//  Camera.
// ---------------------------------------------------------------------------
static void test_camera() {
    Camera2D c;
    c.set_viewport(100, 80);

    // Rigid follow puts the target at the centre.
    c.set_smoothing(1.0f);
    c.follow(Vec2f{200.0f, 150.0f}, 1.0f / 60.0f);
    CHECK(approx(c.centre().x, 200.0f));
    CHECK(approx(c.centre().y, 150.0f));

    // The origin is the top-left of the viewport, on whole pixels — a fractional
    // camera makes every sprite edge shimmer as it moves.
    c.snap_to(Vec2f{200.4f, 150.6f});
    CHECK(approx(c.origin().x, std::floor(200.4f - 50.0f + 0.5f)));
    CHECK(c.origin().x == std::floor(c.origin().x));
    CHECK(c.origin().y == std::floor(c.origin().y));

    // world <-> screen round-trips.
    const Vec2f w{123.0f, 77.0f};
    const Vec2f s = c.world_to_screen(w);
    const Vec2f back = c.screen_to_world(s);
    CHECK(approx(back.x, w.x));
    CHECK(approx(back.y, w.y));

    // Deadzone: inside the box the camera does not move at all.
    Camera2D d;
    d.set_viewport(100, 80);
    d.set_smoothing(1.0f);
    d.set_deadzone(20.0f, 10.0f);
    d.snap_to(Vec2f{100.0f, 100.0f});
    d.follow(Vec2f{115.0f, 105.0f}, 0.1f);      // within the deadzone
    CHECK(approx(d.centre().x, 100.0f));
    CHECK(approx(d.centre().y, 100.0f));
    d.follow(Vec2f{130.0f, 100.0f}, 0.1f);      // 30 away: pulls to 30-20 = 110
    CHECK(approx(d.centre().x, 110.0f));

    // Bounds: the viewport never shows outside the world.
    Camera2D b;
    b.set_viewport(100, 80);
    b.set_smoothing(1.0f);
    b.set_bounds(1000.0f, 800.0f);
    b.snap_to(Vec2f{0.0f, 0.0f});
    CHECK(approx(b.centre().x, 50.0f));         // clamped to half a viewport in
    CHECK(approx(b.centre().y, 40.0f));
    b.snap_to(Vec2f{9999.0f, 9999.0f});
    CHECK(approx(b.centre().x, 950.0f));
    CHECK(approx(b.centre().y, 760.0f));

    // A world SMALLER than the viewport is centred, not pinned to a corner —
    // clamping it would leave all the empty space on one side and read as a bug.
    Camera2D small;
    small.set_viewport(100, 80);
    small.set_smoothing(1.0f);
    small.set_bounds(40.0f, 30.0f);
    small.snap_to(Vec2f{0.0f, 0.0f});
    CHECK(approx(small.centre().x, 20.0f));
    CHECK(approx(small.centre().y, 15.0f));

    // Smoothing is framerate-independent: the same elapsed time reaches the same
    // place whether it arrives in one step or in many. Without this the camera is
    // literally faster on a faster machine.
    Camera2D a1, a2;
    for (Camera2D* c2 : {&a1, &a2}) { c2->set_viewport(100, 80); c2->set_smoothing(0.9f); c2->snap_to(Vec2f{0, 0}); }
    a1.follow(Vec2f{100.0f, 0.0f}, 0.5f);
    for (int i = 0; i < 50; ++i) a2.follow(Vec2f{100.0f, 0.0f}, 0.01f);
    CHECK(approx(a1.centre().x, a2.centre().x, 0.5f));

    // Culling range covers the viewport, padded.
    Camera2D v;
    v.set_viewport(64, 64);
    v.set_smoothing(1.0f);
    v.snap_to(Vec2f{32.0f, 32.0f});             // origin (0,0)
    int x0, y0, x1, y1;
    v.visible_tiles(16, x0, y0, x1, y1, 1);
    CHECK(x0 == -1 && y0 == -1);
    CHECK(x1 == 4 && y1 == 4);                  // 64/16 = 4 tiles + 1 pad
    v.visible_tiles(0, x0, y0, x1, y1);         // degenerate tile size: empty range
    CHECK(x1 < x0);
}

// ---------------------------------------------------------------------------
//  Autotile. The 47 is a RESULT of the canonical rule, not folklore typed in.
// ---------------------------------------------------------------------------
static void test_autotile() {
    CHECK(autotile_count() == 47);

    // Isolated and fully-surrounded are distinct pieces.
    CHECK(autotile_index(0) != autotile_index(0xFF));

    // A diagonal alone changes nothing: with no cardinals beside it there is no
    // corner for it to fill.
    CHECK(autotile_index(kNE) == autotile_index(0));
    CHECK(autotile_index(kNW | kSE) == autotile_index(0));

    // ...but the same diagonal DOES matter once both its cardinals are present.
    CHECK(autotile_index(kN | kE) != autotile_index(kN | kE | kNE));

    // Canonicalisation only ever clears bits, and is idempotent.
    for (int m = 0; m < 256; ++m) {
        const std::uint8_t c = autotile_canonical(static_cast<std::uint8_t>(m));
        CHECK((c & static_cast<std::uint8_t>(m)) == c);
        CHECK(autotile_canonical(c) == c);
    }

    // Every mask maps into range, and equal canonical forms share an index.
    for (int m = 0; m < 256; ++m) {
        const int i = autotile_index(static_cast<std::uint8_t>(m));
        CHECK(i >= 0 && i < 47);
        CHECK(i == autotile_index(autotile_canonical(static_cast<std::uint8_t>(m))));
    }

    // All 47 indices are actually reachable — a table that never emits some of its
    // entries would still pass a range check.
    bool hit[47] = {};
    for (int m = 0; m < 256; ++m) hit[autotile_index(static_cast<std::uint8_t>(m))] = true;
    for (bool b : hit) CHECK(b);
}

int main() {
    test_format();
    test_format_rejects();
    test_migration();
    test_migrate_real_level();
    test_camera();
    test_autotile();
    if (g_failures == 0) std::printf("tilemap: all tests passed\n");
    else                 std::printf("tilemap: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
