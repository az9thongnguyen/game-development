// =============================================================================
//  games/fps/map.cpp
// =============================================================================
#include "games/fps/map.hpp"

namespace fps {

Map default_map() {
    Map m;
    m.w = 16;
    m.h = 16;
    m.cells.assign(static_cast<size_t>(m.w) * m.h, 0);

    auto set = [&](int x, int y, uint8_t v) { m.cells[static_cast<size_t>(y) * m.w + x] = v; };

    // Outer border (wall id 1).
    for (int i = 0; i < 16; ++i) { set(i, 0, 1); set(i, 15, 1); set(0, i, 1); set(15, i, 1); }

    // A central room (wall id 2) with a doorway on its north side.
    for (int x = 5; x <= 10; ++x) { set(x, 5, 2); set(x, 10, 2); }
    for (int y = 5; y <= 10; ++y) { set(5, y, 2); set(10, y, 2); }
    set(7, 5, 0); set(8, 5, 0);  // doorway

    // Four corner pillars (wall id 3).
    set(2, 2, 3); set(13, 2, 3); set(2, 13, 3); set(13, 13, 3);

    return m;
}

} // namespace fps
