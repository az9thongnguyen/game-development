// =============================================================================
//  games/iso/tilemap.cpp  —  TileMap implementation
// =============================================================================
#include "games/iso/tilemap.hpp"

#include <algorithm>

namespace iso {

TileMap::TileMap(int w, int h, Terrain fill)
    : w_(w < 0 ? 0 : w),
      h_(h < 0 ? 0 : h),
      t_(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), fill) {}

Terrain TileMap::at(int x, int y) const {
    if (!in_bounds(x, y)) return Terrain::Grass;
    return t_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + x];
}

void TileMap::set(int x, int y, Terrain t) {
    if (!in_bounds(x, y)) return;
    t_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + x] = t;
}

bool TileMap::terrain_walkable(int x, int y) const {
    return in_bounds(x, y) && at(x, y) != Terrain::Water;
}

void TileMap::fill(Terrain t) { std::fill(t_.begin(), t_.end(), t); }

void TileMap::resize(int w, int h, Terrain fill) {
    w_ = w < 0 ? 0 : w;
    h_ = h < 0 ? 0 : h;
    t_.assign(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), fill);
}

} // namespace iso
