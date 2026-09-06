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


// ---- entities ---------------------------------------------------------------

namespace {

// A stable per-entity merge key, so dragging one spawn is one undo step and moving
// TWO different entities is two. Hashing the name rather than an index because an
// index moves when an entity is created — which is exactly what the first move does.
std::uint64_t entity_key(const std::string& name, std::uint64_t salt) {
    std::uint64_t h = 1469598103934665603ull ^ salt;      // FNV-1a, 64-bit
    for (unsigned char ch : name) { h ^= ch; h *= 1099511628211ull; }
    return h | 1ull;                                       // never 0: 0 means "never merge"
}

tilemap::Entity* find(tilemap::Map& m, const std::string& name) {
    for (auto& e : m.entities)
        if (e.name == name) return &e;
    return nullptr;
}

} // namespace

std::optional<doc::Command> place_entity(tilemap::Map& m, const std::string& name,
                                         int x, int y) {
    if (name.empty() || !m.in_bounds(x, y)) return std::nullopt;

    const tilemap::Entity* cur = find(m, name);
    const bool             existed = cur != nullptr;
    const int              px = existed ? cur->x : 0;
    const int              py = existed ? cur->y : 0;
    if (existed && px == x && py == y) return std::nullopt;   // nothing to undo

    tilemap::Map* mp = &m;
    doc::Command  cmd;
    cmd.label = "move " + name;
    cmd.apply = [mp, name, x, y] {
        if (tilemap::Entity* e = find(*mp, name)) { e->x = x; e->y = y; return; }
        tilemap::Entity e;
        e.name = name;
        e.x = x;
        e.y = y;
        mp->entities.push_back(std::move(e));
    };
    // The two undos are genuinely different. Restoring a position is not enough for
    // an entity that did not exist: leaving it behind at its first cell is a new
    // edit wearing undo's clothes.
    cmd.revert = existed
        ? std::function<void()>([mp, name, px, py] {
              if (tilemap::Entity* e = find(*mp, name)) { e->x = px; e->y = py; }
          })
        : std::function<void()>([mp, name] {
              for (std::size_t i = 0; i < mp->entities.size(); ++i)
                  if (mp->entities[i].name == name) {
                      mp->entities.erase(mp->entities.begin() + static_cast<long>(i));
                      return;
                  }
          });
    // Only a MOVE merges. Creation must stand alone, or the drag that follows it
    // would swallow the creation and undo would restore a position for an entity
    // that should not be there at all.
    cmd.merge_key = existed ? entity_key(name, 0) : 0;
    return cmd;   // NOT applied here: doc::CommandStack::push_apply is what applies
}

std::optional<doc::Command> set_entity_prop(tilemap::Map& m, const std::string& name,
                                            const std::string& key, const std::string& value) {
    if (key.empty()) return std::nullopt;
    tilemap::Entity* e = find(m, name);
    if (e == nullptr) return std::nullopt;

    bool        had = false;
    std::string before;
    for (const auto& p : e->props)
        if (p.key == key) { had = true; before = p.value; break; }
    if (had && before == value) return std::nullopt;

    tilemap::Map* mp = &m;
    doc::Command  cmd;
    cmd.label = name + " " + key;
    cmd.apply = [mp, name, key, value] {
        tilemap::Entity* t = find(*mp, name);
        if (t == nullptr) return;
        for (auto& p : t->props)
            if (p.key == key) { p.value = value; return; }
        t->props.push_back(tilemap::Property{key, value});
    };
    cmd.revert = [mp, name, key, had, before] {
        tilemap::Entity* t = find(*mp, name);
        if (t == nullptr) return;
        for (std::size_t i = 0; i < t->props.size(); ++i)
            if (t->props[i].key == key) {
                if (had) t->props[i].value = before;
                else     t->props.erase(t->props.begin() + static_cast<long>(i));
                return;
            }
    };
    // Same rule as place_entity, for the same reason: CREATING the property stands
    // alone, changing it merges. Otherwise cycling a facing E -> N would collapse
    // into the step that introduced it, and one Ctrl+Z would remove the property
    // rather than step back one value — the gesture and its beginning are not the
    // same edit.
    cmd.merge_key = had ? entity_key(name + "/" + key, 0x9E3779B97F4A7C15ull) : 0;
    return cmd;
}

} // namespace mapedit
