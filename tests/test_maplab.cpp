// =============================================================================
//  tests/test_maplab.cpp  —  Map/Level Lab edit-op core (dependency-free, CTest)
// =============================================================================
//  Verifies the pure grid ops the editor is built on: bordered map construction,
//  bounds-safe set_cell, clamped fill_rect, and a 4-connected flood_fill that
//  respects walls and no-ops on the same id. No SDL/window/assets.
// =============================================================================
#include "games/maplab/edit.hpp"

#include <cstdio>

using namespace maplab;

static int g_failures = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_failures; } \
    } while (0)

int main() {
    // bordered: edges are wall(1), interior floor(0)
    fps::Map m = bordered(5, 4);
    CHECK(m.at(0, 0) == 1 && m.at(4, 3) == 1 && m.at(2, 1) == 0);

    // flood_fill the interior floor -> id 7, stops at the border
    flood_fill(m, 2, 1, 7);
    CHECK(m.at(2, 1) == 7 && m.at(3, 2) == 7);      // interior recoloured
    CHECK(m.at(0, 0) == 1 && m.at(4, 0) == 1);      // border untouched

    // a wall splitting the interior confines the fill to one pocket
    fps::Map w = bordered(5, 4);
    set_cell(w, 2, 1, 1); set_cell(w, 2, 2, 1);     // vertical wall at x=2
    flood_fill(w, 1, 1, 9);
    CHECK(w.at(1, 1) == 9 && w.at(1, 2) == 9);      // left pocket filled
    CHECK(w.at(3, 1) == 0 && w.at(3, 2) == 0);      // right pocket NOT reached

    // same-id flood is a no-op (must not hang)
    flood_fill(w, 3, 1, 0); CHECK(w.at(3, 1) == 0);

    // fill_rect clamps out-of-bounds; set_cell is bounds-safe (no crash)
    fill_rect(m, -3, -3, 100, 100, 2); CHECK(m.at(2, 2) == 2 && m.at(0, 0) == 2);
    set_cell(m, -1, -1, 5); set_cell(m, 999, 0, 5);

    if (g_failures == 0) std::printf("maplab: all tests passed\n");
    else                 std::printf("maplab: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
