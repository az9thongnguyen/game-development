// =============================================================================
//  games/iso/farm.cpp  —  Farm model implementation
// =============================================================================
#include "games/iso/farm.hpp"

#include <cmath>

#include "games/iso/pathfind.hpp"

namespace iso {

Farm::Farm(int w, int h) : map_(w, h, Terrain::Grass) {
    occ_.assign(static_cast<std::size_t>(width()) * static_cast<std::size_t>(height()), kInvalid);
}

// ---- objects ----------------------------------------------------------------
Entity Farm::object_at(int x, int y) const {
    if (!map_.in_bounds(x, y)) return kInvalid;
    return occ_[static_cast<std::size_t>(idx(x, y))];
}

Entity Farm::place_object(int x, int y, ObjKind kind) {
    if (!map_.in_bounds(x, y)) return kInvalid;
    remove_object(x, y);                                  // one object per tile
    const Entity e = world_.create();
    world_.positions.add(e, Position{static_cast<float>(x), static_cast<float>(y)});
    world_.renderables.add(e, Renderable{kind, 0xFFFFFFFFu});
    occ_[static_cast<std::size_t>(idx(x, y))] = e;
    return e;
}

void Farm::remove_object(int x, int y) {
    if (!map_.in_bounds(x, y)) return;
    const std::size_t k = static_cast<std::size_t>(idx(x, y));
    if (occ_[k] != kInvalid) {
        world_.destroy(occ_[k]);
        occ_[k] = kInvalid;
    }
}

// ---- farmer -----------------------------------------------------------------
Vec2i Farm::farmer_cell() const {
    const Position* p = world_.positions.get(farmer_);
    if (!p) return Vec2i{0, 0};
    return Vec2i{static_cast<int>(std::lround(p->x)), static_cast<int>(std::lround(p->y))};
}

Entity Farm::spawn_farmer(int x, int y) {
    if (farmer_ == kInvalid) {
        farmer_ = world_.create();
        world_.renderables.add(farmer_, Renderable{ObjKind::Farmer, 0xFFFFFFFFu});
        world_.movers.add(farmer_, Mover{});
    }
    world_.positions.add(farmer_, Position{static_cast<float>(x), static_cast<float>(y)});
    if (Mover* mv = world_.movers.get(farmer_)) { mv->path.clear(); mv->idx = 0; }
    return farmer_;
}

bool Farm::command_farmer(int gx, int gy) {
    if (farmer_ == kInvalid) return false;
    const Vec2i start = farmer_cell();
    auto walk = [this](int x, int y) { return walkable(x, y); };
    std::vector<Vec2i> path = astar(width(), height(), start, Vec2i{gx, gy}, walk);
    if (path.empty()) return false;                       // unreachable: leave idle

    Mover* mv = world_.movers.get(farmer_);
    if (!mv) return false;
    mv->path = std::move(path);
    mv->idx  = 1;     // path[0] is the current cell; head for the next one
    return true;
}

// ---- simulation -------------------------------------------------------------
bool Farm::walkable(int x, int y) const {
    if (!map_.terrain_walkable(x, y)) return false;
    const Entity e = object_at(x, y);
    if (e == kInvalid) return true;
    const Renderable* r = world_.renderables.get(e);
    return !(r && blocks(r->kind));
}

void Farm::update(double dt) {
    Mover*    mv  = world_.movers.get(farmer_);
    Position* pos = world_.positions.get(farmer_);
    if (!mv || !pos || !mv->moving()) return;

    float remaining = mv->speed * static_cast<float>(dt);
    while (remaining > 0.0f && mv->moving()) {
        const Vec2i tgt = mv->path[mv->idx];
        const float dx = static_cast<float>(tgt.x) - pos->x;
        const float dy = static_cast<float>(tgt.y) - pos->y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= remaining || dist < 1e-5f) {
            pos->x = static_cast<float>(tgt.x);          // arrive: snap to the cell
            pos->y = static_cast<float>(tgt.y);
            remaining -= dist;
            ++mv->idx;                                   // advance to the next cell
        } else {
            pos->x += dx / dist * remaining;             // partial step this tick
            pos->y += dy / dist * remaining;
            remaining = 0.0f;
        }
    }
}

// ---- lifecycle --------------------------------------------------------------
void Farm::reset(int w, int h) {
    map_.resize(w, h, Terrain::Grass);
    world_.clear();
    occ_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), kInvalid);
    farmer_ = kInvalid;
}

void Farm::rebuild_occupancy() {
    occ_.assign(static_cast<std::size_t>(width()) * static_cast<std::size_t>(height()), kInvalid);
    for (const Entity e : world_.alive()) {
        if (e == farmer_) continue;                      // the farmer never occupies
        const Position*   p = world_.positions.get(e);
        const Renderable* r = world_.renderables.get(e);
        if (!p || !r) continue;
        const int x = static_cast<int>(std::lround(p->x));
        const int y = static_cast<int>(std::lround(p->y));
        if (map_.in_bounds(x, y)) occ_[static_cast<std::size_t>(idx(x, y))] = e;
    }
}

void Farm::reset_default() {
    reset(16, 16);

    // A stone path running across the middle.
    for (int x = 0; x < 16; ++x) set_terrain(x, 8, Terrain::Path);
    // A soil patch (for crops) top-left.
    for (int y = 2; y <= 4; ++y)
        for (int x = 2; x <= 6; ++x) set_terrain(x, y, Terrain::Soil);
    // A pond bottom-right.
    for (int y = 11; y <= 13; ++y)
        for (int x = 10; x <= 13; ++x) set_terrain(x, y, Terrain::Water);

    // Wheat on the soil patch.
    for (int y = 2; y <= 4; ++y)
        for (int x = 2; x <= 6; ++x) place_object(x, y, ObjKind::Wheat);

    // A few trees and rocks scattered (deterministic placement).
    const Vec2i trees[] = {{10, 2}, {12, 3}, {13, 5}, {3, 12}, {5, 13}, {1, 10}};
    for (const Vec2i t : trees) place_object(t.x, t.y, ObjKind::Tree);
    const Vec2i rocks[] = {{9, 5}, {14, 9}, {2, 14}};
    for (const Vec2i r : rocks) place_object(r.x, r.y, ObjKind::Rock);

    // A house and a short fence.
    place_object(11, 10, ObjKind::House);
    for (int x = 6; x <= 9; ++x) place_object(x, 11, ObjKind::Fence);

    // The farmer starts on the path.
    spawn_farmer(7, 8);
    rebuild_occupancy();   // belt-and-suspenders: occ already maintained per-edit
}

} // namespace iso
