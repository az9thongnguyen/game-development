// =============================================================================
//  games/fps/raycast.cpp
// =============================================================================
#include "games/fps/raycast.hpp"

#include <cmath>

namespace fps {

Hit cast_ray(const Map& m, double px, double py, double rx, double ry) {
    int mapX = static_cast<int>(px);
    int mapY = static_cast<int>(py);

    const double dX = (rx == 0.0) ? 1e30 : std::fabs(1.0 / rx);
    const double dY = (ry == 0.0) ? 1e30 : std::fabs(1.0 / ry);

    int stepX, stepY;
    double sX, sY;
    if (rx < 0) { stepX = -1; sX = (px - mapX) * dX; }
    else        { stepX = +1; sX = (mapX + 1.0 - px) * dX; }
    if (ry < 0) { stepY = -1; sY = (py - mapY) * dY; }
    else        { stepY = +1; sY = (mapY + 1.0 - py) * dY; }

    int side = 0;
    uint8_t wall = 0;
    for (int guard = 0; guard < 1024 && wall == 0; ++guard) {
        if (sX < sY) { sX += dX; mapX += stepX; side = 0; }
        else         { sY += dY; mapY += stepY; side = 1; }
        wall = m.at(mapX, mapY);
    }

    Hit h;
    h.side  = side;
    h.map_x = mapX;
    h.map_y = mapY;
    h.wall  = wall;
    h.perp_dist = (side == 0) ? (sX - dX) : (sY - dY);
    if (h.perp_dist < 1e-9) h.perp_dist = 1e-9;

    double wx = (side == 0) ? (py + h.perp_dist * ry) : (px + h.perp_dist * rx);
    wx -= std::floor(wx);
    h.wall_x = wx;
    return h;
}

Cam2 project_sprite(double dirX, double dirY, double planeX, double planeY,
                    double relX, double relY) {
    // Inverse of the [plane dir] camera matrix applied to the relative position.
    const double invDet = 1.0 / (planeX * dirY - dirX * planeY);
    Cam2 c;
    c.tx = invDet * (dirY * relX - dirX * relY);
    c.ty = invDet * (-planeY * relX + planeX * relY);  // depth along the view dir
    return c;
}

} // namespace fps
