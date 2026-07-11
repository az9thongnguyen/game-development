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
#include <optional>
#include <string>
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

// Text form (the "fpsmap1" format): shared by the Map/Level Lab editor and the
// raycaster so both sides speak one format. Pure — no IO — the caller reads/writes.
std::string        to_text(const Map& m);
std::optional<Map> from_text(const std::string& s);  // nullopt if malformed

} // namespace fps
