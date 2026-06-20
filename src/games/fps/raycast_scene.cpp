// =============================================================================
//  games/fps/raycast_scene.cpp
// =============================================================================
#include "games/fps/raycast_scene.hpp"

#include <cmath>

#include "engine/color.hpp"

namespace fps {

RaycastScene::RaycastScene()
    : map_(default_map()),
      posX_(3.5), posY_(8.5),     // open spot west of the central room
      dirX_(1.0), dirY_(0.0),     // facing +x (toward the room)
      planeX_(0.0), planeY_(0.66) // ~66 degree field of view
{}

void RaycastScene::try_move(double nx, double ny) {
    const double r = 0.15;  // keep a little distance from walls
    const double mx = nx + (nx - posX_ >= 0 ? r : -r);
    if (map_.at(static_cast<int>(mx), static_cast<int>(posY_)) == 0) posX_ = nx;
    const double my = ny + (ny - posY_ >= 0 ? r : -r);
    if (map_.at(static_cast<int>(posX_), static_cast<int>(my)) == 0) posY_ = ny;
}

void RaycastScene::update(double dt, const platform::InputState& in) {
    using K = platform::Key;
    const double move = 3.0 * dt;   // units/sec
    const double rot  = 2.0 * dt;   // rad/sec

    if (in.down(K::W) || in.down(K::Up))
        try_move(posX_ + dirX_ * move, posY_ + dirY_ * move);
    if (in.down(K::S) || in.down(K::Down))
        try_move(posX_ - dirX_ * move, posY_ - dirY_ * move);
    if (in.down(K::D))   // strafe right (+plane)
        try_move(posX_ + planeX_ * move, posY_ + planeY_ * move);
    if (in.down(K::A))   // strafe left (-plane)
        try_move(posX_ - planeX_ * move, posY_ - planeY_ * move);

    auto rotate = [&](double a) {
        const double c = std::cos(a), s = std::sin(a);
        const double dx = dirX_, px = planeX_;
        dirX_   = dx * c - dirY_ * s;   dirY_   = dx * s + dirY_ * c;
        planeX_ = px * c - planeY_ * s; planeY_ = px * s + planeY_ * c;
    };
    if (in.down(K::Right)) rotate(-rot);  // turn clockwise
    if (in.down(K::Left))  rotate(+rot);  // turn counter-clockwise
}

void RaycastScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const int W = g.width(), H = g.height();

    // Flat ceiling (top half) and floor (bottom half).
    g.fill_rect(0, 0,     W, H / 2,     gfx::rgb(40, 44, 56));
    g.fill_rect(0, H / 2, W, H - H / 2, gfx::rgb(28, 28, 30));

    for (int x = 0; x < W; ++x) {
        // Ray direction for this column: dir + plane * cameraX, cameraX in [-1,1].
        const double cameraX = 2.0 * x / W - 1.0;
        const double rayDirX = dirX_ + planeX_ * cameraX;
        const double rayDirY = dirY_ + planeY_ * cameraX;

        int mapX = static_cast<int>(posX_);
        int mapY = static_cast<int>(posY_);

        const double deltaX = (rayDirX == 0.0) ? 1e30 : std::fabs(1.0 / rayDirX);
        const double deltaY = (rayDirY == 0.0) ? 1e30 : std::fabs(1.0 / rayDirY);

        int stepX, stepY;
        double sideX, sideY;  // distance to the next x / y grid line
        if (rayDirX < 0) { stepX = -1; sideX = (posX_ - mapX) * deltaX; }
        else             { stepX = +1; sideX = (mapX + 1.0 - posX_) * deltaX; }
        if (rayDirY < 0) { stepY = -1; sideY = (posY_ - mapY) * deltaY; }
        else             { stepY = +1; sideY = (mapY + 1.0 - posY_) * deltaY; }

        // DDA: step through grid cells until we hit a wall.
        int side = 0;
        uint8_t hit = 0;
        for (int guard = 0; guard < 512 && hit == 0; ++guard) {
            if (sideX < sideY) { sideX += deltaX; mapX += stepX; side = 0; }
            else               { sideY += deltaY; mapY += stepY; side = 1; }
            hit = map_.at(mapX, mapY);
        }

        // Perpendicular distance (avoids the fisheye a raw ray length would give).
        double perp = (side == 0) ? (sideX - deltaX) : (sideY - deltaY);
        if (perp < 1e-6) perp = 1e-6;

        const int lineH = static_cast<int>(H / perp);
        int start = -lineH / 2 + H / 2; if (start < 0) start = 0;
        int end   =  lineH / 2 + H / 2; if (end >= H) end = H - 1;

        gfx::Color base;
        switch (hit) {
            case 2:  base = gfx::rgb(176, 96, 72);  break;  // room walls (reddish)
            case 3:  base = gfx::rgb(96, 156, 96);  break;  // pillars (greenish)
            default: base = gfx::rgb(150, 150, 162);        // border (grey)
        }
        if (side == 1)  // shade y-side walls darker for a sense of depth
            base = gfx::rgb(gfx::r_of(base) * 3 / 4, gfx::g_of(base) * 3 / 4, gfx::b_of(base) * 3 / 4);

        g.fill_rect(x, start, 1, end - start + 1, base);
    }

    g.draw_text(8, 8, "FPS RAYCASTER  -  WASD move, arrows turn, ESC quit",
                gfx::colors::white, 1);
}

} // namespace fps
