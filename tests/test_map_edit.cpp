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

int main() {
    test_rect_and_flood();
    test_undo_is_exact();
    test_stroke_is_one_step();
    if (g_failures == 0) std::printf("map_edit: all tests passed\n");
    else                 std::printf("map_edit: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
