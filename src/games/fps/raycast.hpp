// =============================================================================
//  games/fps/raycast.hpp  —  pure ray/grid intersection (DDA), testable
// =============================================================================
//  Casting a ray is the heart of the renderer, so we keep it a PURE function
//  (no engine/SDL) that the unit tests can verify directly — the FPS analogue of
//  chess's perft. The Scene calls this once per screen column.
// =============================================================================
#pragma once

#include <cstdint>

#include "games/fps/map.hpp"

namespace fps {

struct Hit {
    double  perp_dist = 1e-9;  // PERPENDICULAR distance to the wall (no fisheye)
    int     side = 0;          // 0 = hit an x-facing wall, 1 = y-facing
    int     map_x = 0, map_y = 0;
    uint8_t wall = 0;          // wall id hit
    double  wall_x = 0.0;      // fractional hit position along the wall [0,1)
};

// Cast a ray from (px,py) in direction (rx,ry) through the grid until a wall.
Hit cast_ray(const Map& m, double px, double py, double rx, double ry);

// Project a sprite (given relative to the player: relX/relY) into camera space.
// tx = horizontal offset (screen x = W/2 * (1 + tx/ty)); ty = depth (>0 = ahead).
struct Cam2 { double tx, ty; };
Cam2 project_sprite(double dirX, double dirY, double planeX, double planeY,
                    double relX, double relY);

} // namespace fps
