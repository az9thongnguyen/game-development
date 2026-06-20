// =============================================================================
//  games/iso/tilemap.hpp  —  the terrain grid (the floor)
// =============================================================================
//  The map's TERRAIN is a DENSE grid: every cell has exactly one terrain type,
//  always. That is the right data model for "the floor", and it is deliberately
//  NOT an entity — contrast it with the sparse ECS (ecs.hpp), which holds the
//  handful of dynamic things that sit ON the floor. Choosing dense-vs-sparse per
//  data shape is the lesson here (see book ch28).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace iso {

enum class Terrain : uint8_t { Grass, Soil, Water, Path };

class TileMap {
public:
    TileMap(int w, int h, Terrain fill = Terrain::Grass);

    int  width()  const { return w_; }
    int  height() const { return h_; }
    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }

    Terrain at(int x, int y) const;        // Grass for out-of-bounds (safe sentinel)
    void    set(int x, int y, Terrain t);  // no-op out of bounds

    // Terrain-only walkability: everything walkable except Water. The Farm layers
    // object-blocking on top of this for the full A* walkability test.
    bool terrain_walkable(int x, int y) const;

    void fill(Terrain t);
    void resize(int w, int h, Terrain fill = Terrain::Grass);

private:
    int                  w_ = 0;
    int                  h_ = 0;
    std::vector<Terrain> t_;   // row-major, size w_*h_
};

} // namespace iso
