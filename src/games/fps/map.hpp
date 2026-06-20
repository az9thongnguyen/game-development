// =============================================================================
//  games/fps/map.hpp  —  the raycaster's grid level
// =============================================================================
//  A raycaster world is just a 2D grid of cells: 0 = empty (you can walk/see
//  through it), >0 = a solid wall (the number is the wall "type", used to pick a
//  color/texture). Out-of-bounds counts as wall so rays + the player can never
//  leave the map.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace fps {

struct Map {
    int                  w = 0;
    int                  h = 0;
    std::vector<uint8_t> cells;  // row-major; 0 = empty, >0 = wall id

    uint8_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= w || y >= h) return 1;  // outside = wall
        return cells[static_cast<size_t>(y) * w + x];
    }
};

Map default_map();  // a small hand-built level

} // namespace fps
