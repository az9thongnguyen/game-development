// =============================================================================
//  engine/tilemap/map_edit.cpp
// =============================================================================
#include "engine/tilemap/map_edit.hpp"

#include <algorithm>
#include <utility>

namespace mapedit {

namespace {

// Write a whole edit list into the map. Used by both apply (after) and revert
// (before), so the two directions cannot drift apart.
void write(tilemap::Map& m, const std::string& layer,
           const std::vector<CellEdit>& cells, bool forward) {
    for (const CellEdit& c : cells) m.set(layer, c.x, c.y, forward ? c.after : c.before);
}

} // namespace

std::vector<CellEdit> rect_cells(const tilemap::Map& m, const std::string& layer,
                                 int x0, int y0, int x1, int y1, std::int32_t id) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, m.w - 1); y1 = std::min(y1, m.h - 1);

    std::vector<CellEdit> out;
    if (m.layer(layer) == nullptr) return out;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            out.push_back(CellEdit{x, y, m.at(layer, x, y), id});
    drop_noops(out);
    return out;
}

std::vector<CellEdit> flood_cells(const tilemap::Map& m, const std::string& layer,
                                  int x, int y, std::int32_t id) {
    std::vector<CellEdit> out;
    if (!m.in_bounds(x, y) || m.layer(layer) == nullptr) return out;
    const std::int32_t from = m.at(layer, x, y);
    if (from == id) return out;              // repainting a region its own colour is
                                             // not an edit; every cell would be a no-op

    // `seen` is a full-map byte mask rather than a set: a flood can cover the whole
    // map, and one byte per cell is cheaper than the tree that would otherwise grow.
    std::vector<char> seen(static_cast<std::size_t>(m.w) * m.h, 0);
    std::vector<std::pair<int, int>> stack{{x, y}};
    while (!stack.empty()) {
        const auto [cx, cy] = stack.back();
        stack.pop_back();
        if (!m.in_bounds(cx, cy)) continue;
        char& mark = seen[static_cast<std::size_t>(cy) * m.w + cx];
        if (mark) continue;
        if (m.at(layer, cx, cy) != from) continue;
        mark = 1;
        out.push_back(CellEdit{cx, cy, from, id});
        stack.push_back({cx + 1, cy});
        stack.push_back({cx - 1, cy});
        stack.push_back({cx, cy + 1});
        stack.push_back({cx, cy - 1});
    }
    return out;
}

void drop_noops(std::vector<CellEdit>& cells) {
    cells.erase(std::remove_if(cells.begin(), cells.end(),
                               [](const CellEdit& c) { return c.before == c.after; }),
                cells.end());
}

std::optional<doc::Command> make_command(tilemap::Map& m, std::string layer,
                                         std::vector<CellEdit> cells, std::string label) {
    drop_noops(cells);
    if (cells.empty()) return std::nullopt;
    tilemap::Map* map = &m;
    return doc::Command{
        std::move(label),
        [map, layer, cells] { write(*map, layer, cells, /*forward*/ true); },
        [map, layer, cells] { write(*map, layer, cells, /*forward*/ false); },
        /*merge_key*/ 0};
}

// ---- Stroke -----------------------------------------------------------------

void Stroke::begin(tilemap::Map& m, std::string layer, std::int32_t id, std::string label) {
    active_ = true;
    map_    = &m;
    layer_  = std::move(layer);
    label_  = std::move(label);
    id_     = id;
    cells_.clear();
}

void Stroke::touch(int x, int y) {
    if (!active_ || map_ == nullptr) return;
    tilemap::Map& m = *map_;
    if (!m.in_bounds(x, y) || m.layer(layer_) == nullptr) return;
    const std::int32_t before = m.at(layer_, x, y);
    // This also rejects a cell the stroke already painted: it now HOLDS id_, so the
    // check above catches the drag coming back over itself. No visited set needed.
    if (before == id_) return;
    cells_.push_back(CellEdit{x, y, before, id_});
    m.set(layer_, x, y, id_);
}

std::optional<doc::Command> Stroke::finish() {
    active_ = false;
    if (cells_.empty() || map_ == nullptr) return std::nullopt;
    // The map is ALREADY at the after-state (touch wrote through), so this command's
    // apply is a no-op the first time and the real thing on redo. It is idempotent by
    // construction, because it writes absolute values rather than a delta.
    std::vector<CellEdit> cells;
    cells.swap(cells_);
    return make_command(*map_, layer_, std::move(cells), label_);
}

} // namespace mapedit
