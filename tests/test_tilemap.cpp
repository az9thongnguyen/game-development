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
#include "engine/assets.hpp"
#include "engine/image.hpp"
#include "engine/tilemap/camera2d.hpp"
#include "engine/tilemap/tileset.hpp"
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
    CHECK(!load("map2 3\nname x\nsize 1 1\ntile 8\n").has_value());
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
// fixture that has drifted away from what --fps actually loads. It is map2 now
// (chapter 132); `load` is still the one entry point, which is the property being
// checked here — the caller does not know or care which era the file came from.
static void test_migrate_real_level() {
    std::ifstream f(std::string(ASSET_ROOT) + "/assets/maps/level_00.map2");
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

    // Setting the VIEWPORT or the BOUNDS re-clamps. Bounds are a relationship between
    // the world and the view, so a camera that was legal for the old size is not
    // automatically legal for the new one — and a scene learns its framebuffer size
    // only when it first draws, which is after it has already snapped the camera.
    // Without this the first frame of a world smaller than the window is off-centre.
    {
        Camera2D late;
        late.set_bounds(384.0f, 288.0f);
        late.snap_to(Vec2f{72.0f, 136.0f});     // no viewport known yet
        late.set_viewport(640, 360);            // ...now it is
        // The world is smaller than the viewport, so it must be CENTRED.
        CHECK(approx(late.centre().x, 384.0f / 2.0f));
        CHECK(approx(late.centre().y, 288.0f / 2.0f));

        Camera2D shrink;
        shrink.set_viewport(100, 80);
        shrink.set_bounds(1000.0f, 1000.0f);
        shrink.snap_to(Vec2f{990.0f, 990.0f});
        CHECK(approx(shrink.centre().x, 950.0f));    // clamped to the far edge
        shrink.set_viewport(400, 400);               // a much wider view
        CHECK(approx(shrink.centre().x, 800.0f));    // ...re-clamped for it
    }

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
// -----------------------------------------------------------------------------
//  The other rule: a LINE, for a material one tile wide. Chapter 125 needed it
//  because the 47-piece blob rule is for an AREA — down a one-wide path no diagonal
//  ever has both its cardinals filled, so only 16 of the 47 are reachable and 31
//  slots of the sheet could never be drawn.
// -----------------------------------------------------------------------------
static void test_autotile_line() {
    CHECK(kLinePieces == 16);

    // The numbering IS the sheet layout, so it is spelled out rather than derived.
    // Reordering these bits would silently swap a corner for a T-junction, and every
    // "the pieces are distinct" style of test would still pass.
    CHECK(autotile_line_index(0) == 0);
    CHECK(autotile_line_index(kN) == 1);
    CHECK(autotile_line_index(kE) == 2);
    CHECK(autotile_line_index(kS) == 4);
    CHECK(autotile_line_index(kW) == 8);
    CHECK(autotile_line_index(kN | kS) == 5);                    // the vertical run
    CHECK(autotile_line_index(kE | kW) == 10);                   // ...and the horizontal
    CHECK(autotile_line_index(kN | kE | kS | kW) == 15);         // the crossroads

    // Diagonals are IGNORED, not folded. For a line they carry no information: the
    // piece is decided entirely by which of the four arms leave the tile.
    CHECK(autotile_line_index(kN | kNE) == autotile_line_index(kN));
    CHECK(autotile_line_index(0xFFu) == autotile_line_index(kN | kE | kS | kW));
    CHECK(autotile_line_index(kNE | kSE | kSW | kNW) == 0);

    // Every index in range, every index reachable, and no two cardinal
    // neighbourhoods sharing one — a set of pieces where two shapes collide is a set
    // with a piece nobody can draw.
    bool hit[16] = {};
    for (int m = 0; m < 256; ++m) {
        const int i = autotile_line_index(static_cast<std::uint8_t>(m));
        CHECK(i >= 0 && i < kLinePieces);
        hit[i] = true;
    }
    for (const bool h : hit) CHECK(h);
}

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


// -----------------------------------------------------------------------------
//  Cutting a sheet into tiles. The interesting cases are the edges: an index past
//  the end must be ASKABLE (a null sprite, not a crash), and a sheet that is not an
//  exact multiple of the tile size must drop the partial cells rather than pad them
//  with whatever follows in memory.
// -----------------------------------------------------------------------------
// ---- chapter 134: rules live in the MAP ------------------------------------
static void test_rules_roundtrip_and_version() {
    // A map with no rules is still a v1 file: its bytes — and therefore its release
    // id — must not move for a feature it does not use.
    Map plain;
    plain.name = "p"; plain.w = plain.h = 2; plain.tile = 8;
    plain.layers.push_back(Layer{"ground", LayerKind::Tiles, "", {1, 1, 1, 1}, {}});
    CHECK(to_text(plain).rfind("map2 1\n", 0) == 0);

    Map m;
    m.name = "r"; m.w = 3; m.h = 3; m.tile = 8;
    Layer g{"ground", LayerKind::Tiles, "", {}, {}};
    g.cells.assign(9, 0);
    g.rules.push_back(Rule{2, RuleKind::Line});
    g.rules.push_back(Rule{5, RuleKind::Blob});
    m.layers.push_back(std::move(g));
    const std::string text = to_text(m);
    CHECK(text.rfind("map2 2\n", 0) == 0);
    CHECK(text.find("rule 2 line\nrule 5 blob\nrow") != std::string::npos);

    auto back = load(text);
    CHECK(back.has_value());
    if (back) {
        CHECK(to_text(*back) == text);
        CHECK(back->rule_for("ground", 2) == RuleKind::Line);
        CHECK(back->rule_for("ground", 5) == RuleKind::Blob);
        CHECK(back->rule_for("ground", 9) == RuleKind::None);   // no rule is an answer
        CHECK(back->rule_for("nosuch",  2) == RuleKind::None);
    }

    // The refusals. Each is a file that would otherwise mean two things at once.
    const char* head = "map2 2\nname x\nsize 2 1\ntile 8\nlayer g tiles -\n";
    CHECK(!load(std::string(head) + "rule 0 line\nrow 1 1\n").has_value());       // 0 is empty
    CHECK(!load(std::string(head) + "rule 2 blob\nrule 2 line\nrow 1 1\n").has_value());
    CHECK(!load(std::string(head) + "rule 2 spiral\nrow 1 1\n").has_value());
    // A rule BETWEEN rows. The map has to be taller than one row for this to be the
    // case it means: with a single row the grid is already complete when the rule
    // appears, and the outer parser refuses it as an unknown directive for a
    // different reason — which made this look tested when it was not.
    CHECK(!load("map2 2\nname x\nsize 1 2\ntile 8\nlayer g tiles -\n"
                "row 1\nrule 2 line\nrow 1\n").has_value());
    CHECK(!load(std::string(head) + "row 1 1\nrule 2 line\n").has_value());       // after the grid
    // ...and a v1 file with no rules still loads, because that is most of them.
    CHECK(load("map2 1\nname x\nsize 2 1\ntile 8\nlayer g tiles -\nrow 1 1\n").has_value());
}

// The one implementation, replacing farm::line_piece: a road's four neighbours, a
// region's eight, and 0 for a cell whose value nobody gave a rule.
static void test_rule_piece() {
    Map m;
    m.name = "r"; m.w = 3; m.h = 3; m.tile = 8;
    Layer g{"ground", LayerKind::Tiles, "", {}, {}};
    g.cells = {0, 2, 0,
               2, 2, 2,
               0, 2, 0};
    g.rules.push_back(Rule{2, RuleKind::Line});
    m.layers.push_back(std::move(g));

    // The centre continues on all four sides: the crossroads, piece 15 of 16.
    CHECK(rule_piece(m, "ground", 1, 1) == 15);
    // The north arm continues only southward: piece 4 (south bit).
    CHECK(rule_piece(m, "ground", 1, 0) == 4);
    // Out of bounds does NOT connect, so the arms are end caps, not through-runs.
    CHECK(rule_piece(m, "ground", 0, 1) == 2);          // east only
    CHECK(rule_piece(m, "ground", 0, 0) == 0);          // an empty cell: nothing
    CHECK(neighbour_mask(m, "ground", 1, 1) == (kN | kE | kS | kW));

    // The contract for an EMPTY cell, which is the one place the bounds check earns
    // its keep: Map::at answers 0 off the map, so without it a hole in the middle of
    // nothing would report the void beyond the edge as more of itself.
    CHECK(m.at("ground", 0, 0) == 0);
    CHECK((neighbour_mask(m, "ground", 0, 0) & (kN | kW | kNW | kNE | kSW)) == 0);

    // Drop the rule and every one of those becomes 0 — the number a caller adds to a
    // base without asking whether there was a rule.
    m.layers[0].rules.clear();
    CHECK(rule_piece(m, "ground", 1, 1) == 0);
    CHECK(rule_piece(m, "ground", 1, 0) == 0);

    // A region reads its diagonals too, so the same cross is a different piece.
    m.layers[0].rules.push_back(Rule{2, RuleKind::Blob});
    CHECK(rule_piece(m, "ground", 1, 1) == autotile_index(kN | kE | kS | kW));
    CHECK(rule_piece(m, "ground", 1, 1) != 15);
    m.set("ground", 0, 0, 2);                            // fill a corner
    CHECK(neighbour_mask(m, "ground", 1, 1) == (kN | kE | kS | kW | kNW));
    // ...which the LINE rule would have ignored entirely. That is the difference.
    CHECK(autotile_line_index(neighbour_mask(m, "ground", 1, 1)) == 15);
}

// The sixteen neighbourhoods, and then the farm's own path. Moved here from
// tests/test_farm.cpp in chapter 134 with the function it checks: the five cells
// below are the ones that make that path a path rather than a row of squares, and
// they are read off a real committed map, which is the check that outlives whichever
// module owns the chooser.
//
// The whole-frame test in test_farm_scene can only prove the picture CHANGES from
// cell to cell. That left two mutations alive — "never look north" and "any neighbour
// connects, not just the same id" — both of which still produce a picture that varies
// per cell, just the wrong one. So the answer is checked here, by value.
static void test_rule_piece_sixteen_and_the_real_path() {
    // A 3x3 map with the cell under test in the middle. Every DIAGONAL is set to the
    // same id in all sixteen cases: for a line they carry no information, and a rule
    // that quietly consulted one would answer sixteen different questions here.
    const auto probe = [](int bits) {
        const int n = bits & 1, e = bits & 2, sth = bits & 4, w = bits & 8;
        std::string text = "map2 2\nname probe\nsize 3 3\ntile 16\nlayer ground tiles -\n"
                           "rule 2 line\n";
        text += std::string("row 2 ") + (n ? "2" : "1") + " 2\n";
        text += std::string("row ") + (w ? "2" : "1") + " 2 " + (e ? "2" : "1") + "\n";
        text += std::string("row 2 ") + (sth ? "2" : "1") + " 2\n";
        const auto m = load(text);
        CHECK(m.has_value());
        return m ? rule_piece(*m, "ground", 1, 1) : -1;
    };
    for (int bits = 0; bits < 16; ++bits) CHECK(probe(bits) == bits);

    // The farm's own path, at the five cells that make it a path. Read off
    // assets/maps/farm_home.map2: a vertical run down column 4 from row 6, turning
    // east along row 11 and ending at column 16.
    assets::set_base_path(ASSET_ROOT "/assets");
    const auto bytes = assets::load_file("maps/farm_home.map2");
    CHECK(bytes.has_value());
    if (!bytes) return;
    const auto map = load(std::string(bytes->begin(), bytes->end()));
    CHECK(map.has_value());
    if (!map) return;
    // The rule is in the FILE now, not in the game that reads it. If that line ever
    // goes missing every assertion below collapses to 0 at once.
    CHECK(map->rule_for("ground", 2) == RuleKind::Line);
    const auto at = [&](int x, int y) { return rule_piece(*map, "ground", x, y); };
    CHECK(at(4, 6)   == 4);    // the north end cap: only south continues
    CHECK(at(4, 8)   == 5);    // ...a straight vertical run
    CHECK(at(4, 11)  == 3);    // the corner: north and east
    CHECK(at(10, 11) == 10);   // ...a straight horizontal run
    CHECK(at(16, 11) == 8);    // the east end cap

    // The grass fills the map and has NO rule, so every one of its cells answers 0 —
    // which is the number a renderer adds to a base without asking first.
    CHECK(map->rule_for("ground", 1) == RuleKind::None);
    CHECK(at(0, 0) == 0);
}

static void test_tileset() {
    // A 3x2 grid of 4px tiles, each filled with its own index so a mis-cut is
    // visible as a wrong colour rather than as a plausible picture.
    gfx::Image sheet;
    sheet.w = 12;
    sheet.h = 8;
    sheet.pixels.resize(96);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 12; ++x)
            sheet.pixels[static_cast<std::size_t>(y) * 12 + static_cast<std::size_t>(x)] =
                static_cast<gfx::Color>(0xFF000000u + static_cast<unsigned>((y / 4) * 3 + (x / 4)));

    const tilemap::Tileset ts = tilemap::Tileset::cut(sheet, 4);
    CHECK(ts.count() == 6);
    CHECK(ts.tile() == 4);
    CHECK(ts.columns() == 3);

    for (std::size_t i = 0; i < 6; ++i) {
        const gfx::Sprite sp = ts.sprite(i);
        CHECK(sp.w == 4 && sp.h == 4 && sp.pixels != nullptr);
        // EVERY pixel of the cell, not just the first: a cut that reads the wrong
        // row would still start correctly.
        bool all = true;
        for (int k = 0; k < 16; ++k)
            all = all && sp.pixels[k] == static_cast<gfx::Color>(0xFF000000u + static_cast<unsigned>(i));
        CHECK(all);
    }

    // Out of range is a question, not a crash.
    CHECK(ts.sprite(6).w == 0 && ts.sprite(6).pixels == nullptr);
    CHECK(ts.sprite(9999).w == 0);

    // 13x9 holds the same 3x2 whole tiles; the leftover column and row are dropped.
    gfx::Image ragged = sheet;
    ragged.w = 13;
    ragged.h = 9;
    ragged.pixels.resize(13 * 9);
    CHECK(tilemap::Tileset::cut(ragged, 4).count() == 6);

    // Degenerate inputs yield an empty set rather than an exception or a huge loop.
    CHECK(tilemap::Tileset::cut(sheet, 0).count() == 0);
    CHECK(tilemap::Tileset::cut(sheet, 99).count() == 0);
    CHECK(tilemap::Tileset::cut(gfx::Image{}, 4).count() == 0);

    // The real sheet this project ships: 192x176 of 16px tiles is 12 x 11 = 132.
    assets::set_base_path(ASSET_ROOT "/assets");
    if (const auto real = gfx::load_image("textures/town.hrt")) {
        const tilemap::Tileset town = tilemap::Tileset::cut(*real, 16);
        CHECK(town.count() == 132);
        CHECK(town.columns() == 12);
        // Tile 0 is solid ground: opaque everywhere. Tile 28 is a tree on a
        // transparent background. If the cut were off by a row those would swap.
        int clear0 = 0, clear28 = 0;
        for (int i = 0; i < 256; ++i) {
            if ((town.sprite(0).pixels[i]  >> 24) == 0) ++clear0;
            if ((town.sprite(28).pixels[i] >> 24) == 0) ++clear28;
        }
        CHECK(clear0 == 0);
        CHECK(clear28 > 0);
    } else {
        std::printf("FAIL: textures/town.hrt did not load\n");
        ++g_failures;
    }
}

int main() {
    test_format();
    test_rules_roundtrip_and_version();
    test_rule_piece();
    test_rule_piece_sixteen_and_the_real_path();
    test_tileset();
    test_format_rejects();
    test_migration();
    test_migrate_real_level();
    test_camera();
    test_autotile();
    test_autotile_line();
    if (g_failures == 0) std::printf("tilemap: all tests passed\n");
    else                 std::printf("tilemap: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
