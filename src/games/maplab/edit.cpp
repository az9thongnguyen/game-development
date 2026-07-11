// =============================================================================
//  games/maplab/edit.cpp
// =============================================================================
#include "games/maplab/edit.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace maplab {

namespace {
bool in(const fps::Map& m, int x, int y) { return x >= 0 && y >= 0 && x < m.w && y < m.h; }
uint8_t& cell(fps::Map& m, int x, int y) { return m.cells[static_cast<size_t>(y) * m.w + x]; }
} // namespace

void set_cell(fps::Map& m, int x, int y, uint8_t id) {
    if (in(m, x, y)) cell(m, x, y) = id;
}

void fill_rect(fps::Map& m, int x0, int y0, int x1, int y1, uint8_t id) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) set_cell(m, x, y, id);
}

void flood_fill(fps::Map& m, int x, int y, uint8_t id) {
    if (!in(m, x, y)) return;
    const uint8_t from = cell(m, x, y);
    if (from == id) return;                         // no-op guard (else an infinite scan)
    std::vector<std::pair<int, int>> stack{{x, y}};
    while (!stack.empty()) {
        auto [cx, cy] = stack.back(); stack.pop_back();
        if (!in(m, cx, cy) || cell(m, cx, cy) != from) continue;
        cell(m, cx, cy) = id;
        stack.push_back({cx + 1, cy}); stack.push_back({cx - 1, cy});
        stack.push_back({cx, cy + 1}); stack.push_back({cx, cy - 1});
    }
}

fps::Map bordered(int w, int h) {
    fps::Map m; m.w = w; m.h = h; m.cells.assign(static_cast<size_t>(w) * h, 0);
    fill_rect(m, 0, 0, w - 1, 0, 1);       // top
    fill_rect(m, 0, h - 1, w - 1, h - 1, 1); // bottom
    fill_rect(m, 0, 0, 0, h - 1, 1);       // left
    fill_rect(m, w - 1, 0, w - 1, h - 1, 1); // right
    return m;
}

} // namespace maplab
