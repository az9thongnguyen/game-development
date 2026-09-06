// =============================================================================
//  games/fps/map.cpp
// =============================================================================
#include "games/fps/map.hpp"

#include <cstdlib>
#include <sstream>

#include "engine/tilemap/map2.hpp"

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

    m.spawn_cx = 3; m.spawn_cy = 8; m.spawn_dir = 0.0f;  // matches the legacy (3.5,8.5,+x) start
    return m;
}

std::optional<Map> from_shared_text(const std::string& s) {
    auto tm = tilemap::load(s);
    if (!tm) return std::nullopt;

    Map m;
    m.w = tm->w;
    m.h = tm->h;
    m.cells.assign(static_cast<size_t>(m.w) * m.h, 0);
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            const std::int32_t id = tm->at("wall", x, y);
            m.cells[static_cast<size_t>(y) * m.w + x] =
                static_cast<uint8_t>(id < 0 ? 0 : (id > 255 ? 255 : id));
        }

    if (const tilemap::Entity* sp = tm->entity("spawn_player")) {
        m.spawn_cx  = sp->x;
        m.spawn_cy  = sp->y;
        m.spawn_dir = std::strtof(tilemap::prop(sp->props, "dir", "0").c_str(), nullptr);
    }
    return m;
}

} // namespace fps
