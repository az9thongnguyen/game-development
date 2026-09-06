// =============================================================================
//  tests/test_map_edit.cpp  —  tile edits as undoable commands
// =============================================================================
//  Pure: a map in memory, a command stack, no renderer and no files. The property
//  that matters is not "paint changes a cell" but "undo restores exactly what was
//  there" — including a stroke that crossed several different tile values.
// =============================================================================
#include <cstdio>
#include <string>
#include <vector>

#include "engine/document/command_stack.hpp"
#include "engine/tilemap/map_edit.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

tilemap::Map make_map(int w, int h) {
    tilemap::Map m;
    m.name = "t"; m.w = w; m.h = h; m.tile = 16;
    tilemap::Layer ground;
    ground.name = "ground"; ground.kind = tilemap::LayerKind::Tiles;
    ground.cells.assign(static_cast<std::size_t>(w) * h, 0);
    m.layers.push_back(std::move(ground));
    return m;
}

// A snapshot of every cell, for "undo restored the document exactly" checks.
std::vector<std::int32_t> snapshot(const tilemap::Map& m, const char* layer) {
    std::vector<std::int32_t> v;
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) v.push_back(m.at(layer, x, y));
    return v;
}

} // namespace

static void test_rect_and_flood() {
    tilemap::Map m = make_map(8, 6);

    auto r = mapedit::rect_cells(m, "ground", 1, 1, 3, 2, 5);
    CHECK(r.size() == 6);                        // 3 wide x 2 tall

    // Corners in any order, and a rect that hangs off the map is clamped, not
    // rejected: a drag that leaves the canvas should still paint what it covered.
    CHECK(mapedit::rect_cells(m, "ground", 3, 2, 1, 1, 5).size() == 6);
    CHECK(mapedit::rect_cells(m, "ground", -4, -4, 1, 1, 5).size() == 4);
    CHECK(mapedit::rect_cells(m, "ground", 6, 4, 99, 99, 5).size() == 4);

    // An unknown layer produces no edits rather than an out-of-range write.
    CHECK(mapedit::rect_cells(m, "nope", 0, 0, 2, 2, 5).empty());

    // Painting a value a cell already holds is not an edit.
    CHECK(mapedit::rect_cells(m, "ground", 0, 0, 2, 2, 0).empty());

    // Flood over an empty map covers everything; over a map divided by a wall it
    // stops at the wall — the whole point of a 4-connected fill.
    CHECK(mapedit::flood_cells(m, "ground", 0, 0, 7).size() == 48);
    for (int y = 0; y < 6; ++y) m.set("ground", 4, y, 1);
    CHECK(mapedit::flood_cells(m, "ground", 0, 0, 7).size() == 24);   // left half only
    CHECK(mapedit::flood_cells(m, "ground", 5, 0, 7).size() == 18);   // right half (3 cols)

    // Flooding a region with the value it already has terminates and does nothing.
    CHECK(mapedit::flood_cells(m, "ground", 4, 0, 1).empty());
    CHECK(mapedit::flood_cells(m, "ground", 99, 0, 7).empty());
}

static void test_undo_is_exact() {
    tilemap::Map m = make_map(8, 6);
    doc::CommandStack st;

    // A varied starting state, so a revert that wrote a single constant would fail.
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 8; ++x) m.set("ground", x, y, (x + y * 3) % 4);
    const auto before = snapshot(m, "ground");

    auto cmd = mapedit::make_command(m, "ground", mapedit::rect_cells(m, "ground", 1, 1, 5, 4, 9),
                                     "fill");
    CHECK(cmd.has_value());
    st.push_apply(*cmd);
    CHECK(m.at("ground", 3, 3) == 9);
    CHECK(snapshot(m, "ground") != before);

    CHECK(st.undo());
    CHECK(snapshot(m, "ground") == before);      // exactly, not approximately
    CHECK(st.redo());
    CHECK(m.at("ground", 3, 3) == 9);

    // Nothing to do -> no command, so an empty step never reaches the stack.
    CHECK(!mapedit::make_command(m, "ground", {}, "empty").has_value());
    CHECK(!mapedit::make_command(m, "ground",
                                 mapedit::rect_cells(m, "ground", 1, 1, 5, 4, 9), "again")
               .has_value());
}

static void test_stroke_is_one_step() {
    tilemap::Map m = make_map(8, 6);
    doc::CommandStack st;
    for (int x = 0; x < 8; ++x) m.set("ground", x, 0, x);   // every cell different
    const auto before = snapshot(m, "ground");

    mapedit::Stroke s;
    s.begin(m, "ground", 9, "paint");
    CHECK(s.active());
    for (int x = 0; x < 8; ++x) s.touch(x, 0);
    s.touch(3, 0);                       // dragging back over a cell does not re-record
    s.touch(-1, 0);                      // off the map
    s.touch(0, 99);
    CHECK(s.touched() == 8);             // eight distinct cells, recorded once each
    CHECK(m.at("ground", 5, 0) == 9);    // written through as the drag happens

    auto cmd = s.finish();
    CHECK(!s.active());
    CHECK(cmd.has_value());
    st.push_apply(*cmd);
    CHECK(st.undo_depth() == 1);         // ONE step for the whole gesture
    CHECK(m.at("ground", 5, 0) == 9);    // push_apply re-applied: idempotent

    CHECK(st.undo());
    CHECK(snapshot(m, "ground") == before);   // every distinct value restored
    CHECK(st.redo());
    for (int x = 0; x < 8; ++x) CHECK(m.at("ground", x, 0) == 9);

    // A gesture that changed nothing produces no command at all.
    mapedit::Stroke empty;
    empty.begin(m, "ground", 9, "paint");
    empty.touch(0, 0);                   // already 9
    CHECK(!empty.finish().has_value());

    // A stroke on a layer that does not exist is inert, not a crash.
    mapedit::Stroke bad;
    bad.begin(m, "nope", 1, "paint");
    bad.touch(0, 0);
    CHECK(!bad.finish().has_value());
}

// ---------------------------------------------------------------------------
//  Entities. The half of a map that is not a grid, and the reason the old Map Lab
//  outlived the Map workspace by ten chapters.
// ---------------------------------------------------------------------------
static void test_place_entity_creates_then_moves() {
    tilemap::Map      m = make_map(8, 8);
    doc::CommandStack st;

    CHECK(m.entities.empty());
    auto c1 = mapedit::place_entity(m, "spawn_player", 2, 3);
    CHECK(c1.has_value());
    if (c1) st.push_apply(*c1);
    CHECK(m.entities.size() == 1);
    CHECK(m.entities[0].name == "spawn_player");
    CHECK(m.entities[0].x == 2 && m.entities[0].y == 3);

    // Moving it is a move, not a second spawn.
    auto c2 = mapedit::place_entity(m, "spawn_player", 5, 1);
    CHECK(c2.has_value());
    if (c2) st.push_apply(*c2);
    CHECK(m.entities.size() == 1);
    CHECK(m.entities[0].x == 5 && m.entities[0].y == 1);

    // Undo the move: back to the first cell, still exactly one entity.
    CHECK(st.undo());
    CHECK(m.entities.size() == 1);
    CHECK(m.entities[0].x == 2 && m.entities[0].y == 3);

    // Undo the CREATION: it must go away. Leaving it at its first cell would be a
    // different edit wearing undo's clothes — the map would keep a spawn the author
    // never placed, and nothing downstream could tell.
    CHECK(st.undo());
    CHECK(m.entities.empty());

    CHECK(st.redo());
    CHECK(m.entities.size() == 1 && m.entities[0].x == 2);
}

static void test_a_drag_is_one_undo_step_but_the_creation_is_not() {
    tilemap::Map      m = make_map(8, 8);
    doc::CommandStack st;

    if (auto c = mapedit::place_entity(m, "spawn_player", 0, 0)) st.push_apply(*c);
    // A drag: one command per frame, all merging into one step.
    for (int x = 1; x <= 4; ++x)
        if (auto c = mapedit::place_entity(m, "spawn_player", x, 0)) st.push_apply(*c);
    CHECK(m.entities[0].x == 4);

    CHECK(st.undo());
    CHECK(m.entities[0].x == 0);         // the whole drag, in one press
    CHECK(st.undo());
    CHECK(m.entities.empty());           // ...and the creation did NOT merge into it
    CHECK(!st.undo());

    // Two different entities do not share a step, or moving one would undo the other.
    doc::CommandStack st2;
    tilemap::Map      m2 = make_map(8, 8);
    if (auto c = mapedit::place_entity(m2, "a", 0, 0)) st2.push_apply(*c);
    if (auto c = mapedit::place_entity(m2, "b", 1, 1)) st2.push_apply(*c);
    if (auto c = mapedit::place_entity(m2, "a", 2, 0)) st2.push_apply(*c);
    if (auto c = mapedit::place_entity(m2, "b", 3, 1)) st2.push_apply(*c);
    CHECK(st2.undo());
    CHECK(m2.entity("b")->x == 1);
    CHECK(m2.entity("a")->x == 2);       // b moved back, a did not
}

static void test_entity_refusals() {
    tilemap::Map m = make_map(4, 4);
    CHECK(!mapedit::place_entity(m, "spawn_player", -1, 0).has_value());
    CHECK(!mapedit::place_entity(m, "spawn_player", 4, 0).has_value());
    CHECK(!mapedit::place_entity(m, "spawn_player", 0, 4).has_value());
    CHECK(!mapedit::place_entity(m, "", 0, 0).has_value());
    CHECK(m.entities.empty());           // and not one of them created anything

    // The command comes back UNAPPLIED, so it has to be run before the map knows
    // anything happened — a caller that only inspects the optional has changed nothing.
    auto first = mapedit::place_entity(m, "spawn_player", 1, 1);
    CHECK(first.has_value());
    if (first) first->apply();
    CHECK(m.entities.size() == 1);
    // Placing it where it already is changes nothing, so it must not become a step:
    // an undo that appears to do nothing is indistinguishable from a broken undo.
    CHECK(!mapedit::place_entity(m, "spawn_player", 1, 1).has_value());
}

static void test_entity_property_is_undoable() {
    tilemap::Map      m = make_map(4, 4);
    doc::CommandStack st;
    // No entity, no property: a facing on a spawn that does not exist would be a
    // silent no-op the author never sees.
    CHECK(!mapedit::set_entity_prop(m, "spawn_player", "facing", "E").has_value());

    if (auto c = mapedit::place_entity(m, "spawn_player", 1, 1)) st.push_apply(*c);
    auto p1 = mapedit::set_entity_prop(m, "spawn_player", "facing", "E");
    CHECK(p1.has_value());
    if (p1) st.push_apply(*p1);
    const tilemap::Entity* sp = m.entity("spawn_player");
    CHECK(sp != nullptr);
    if (sp == nullptr) return;                 // guard: a null here must FAIL, not crash
    CHECK(sp->props.size() == 1);
    if (sp->props.size() == 1) CHECK(sp->props[0].value == "E");

    CHECK(!mapedit::set_entity_prop(m, "spawn_player", "facing", "E").has_value());  // same value

    auto p2 = mapedit::set_entity_prop(m, "spawn_player", "facing", "N");
    CHECK(p2.has_value());
    if (p2) st.push_apply(*p2);
    if (sp->props.size() == 1) CHECK(sp->props[0].value == "N");
    if (auto p3 = mapedit::set_entity_prop(m, "spawn_player", "facing", "W")) st.push_apply(*p3);
    if (sp->props.size() == 1) CHECK(sp->props[0].value == "W");

    // One step for the CYCLE (N -> W merged), landing back on E. The step that
    // introduced the property is deliberately not part of it — collapsing them would
    // make the first Ctrl+Z remove a facing rather than step back one value.
    CHECK(st.undo());
    CHECK(sp->props.size() == 1);
    if (sp->props.size() == 1) CHECK(sp->props[0].value == "E");

    // Undo again and the property must be GONE, not empty-valued: it did not exist.
    CHECK(st.undo());
    CHECK(sp->props.empty());
}

int main() {
    test_rect_and_flood();
    test_undo_is_exact();
    test_stroke_is_one_step();
    test_place_entity_creates_then_moves();
    test_a_drag_is_one_undo_step_but_the_creation_is_not();
    test_entity_refusals();
    test_entity_property_is_undoable();
    if (g_failures == 0) std::printf("map_edit: all tests passed\n");
    else                 std::printf("map_edit: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
