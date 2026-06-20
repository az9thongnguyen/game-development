// =============================================================================
//  games/fps/raycast_scene.cpp
// =============================================================================
#include "games/fps/raycast_scene.hpp"

#include <cmath>

#include "engine/color.hpp"
#include "games/fps/raycast.hpp"

namespace fps {

RaycastScene::RaycastScene()
    : map_(default_map()),
      textures_(make_wall_textures()),
      posX_(3.5), posY_(8.5),
      dirX_(1.0), dirY_(0.0),
      planeX_(0.0), planeY_(0.66)
{}

void RaycastScene::try_move(double nx, double ny) {
    const double r = 0.15;
    const double mx = nx + (nx - posX_ >= 0 ? r : -r);
    if (map_.at(static_cast<int>(mx), static_cast<int>(posY_)) == 0) posX_ = nx;
    const double my = ny + (ny - posY_ >= 0 ? r : -r);
    if (map_.at(static_cast<int>(posX_), static_cast<int>(my)) == 0) posY_ = ny;
}

void RaycastScene::update(double dt, const platform::InputState& in) {
    using K = platform::Key;
    const double move = 3.0 * dt;
    const double rot  = 2.0 * dt;

    if (in.down(K::W) || in.down(K::Up))   try_move(posX_ + dirX_ * move, posY_ + dirY_ * move);
    if (in.down(K::S) || in.down(K::Down)) try_move(posX_ - dirX_ * move, posY_ - dirY_ * move);
    if (in.down(K::D))                     try_move(posX_ + planeX_ * move, posY_ + planeY_ * move);
    if (in.down(K::A))                     try_move(posX_ - planeX_ * move, posY_ - planeY_ * move);

    auto rotate = [&](double a) {
        const double c = std::cos(a), s = std::sin(a);
        const double dx = dirX_, px = planeX_;
        dirX_   = dx * c - dirY_ * s;   dirY_   = dx * s + dirY_ * c;
        planeX_ = px * c - planeY_ * s; planeY_ = px * s + planeY_ * c;
    };
    if (in.down(K::Right)) rotate(-rot);
    if (in.down(K::Left))  rotate(+rot);
}

void RaycastScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const int W = g.width(), H = g.height();

    g.fill_rect(0, 0,     W, H / 2,     gfx::rgb(48, 52, 66));  // ceiling
    g.fill_rect(0, H / 2, W, H - H / 2, gfx::rgb(26, 26, 28));  // floor

    for (int x = 0; x < W; ++x) {
        const double cameraX = 2.0 * x / W - 1.0;
        const double rayDirX = dirX_ + planeX_ * cameraX;
        const double rayDirY = dirY_ + planeY_ * cameraX;
        const Hit h = cast_ray(map_, posX_, posY_, rayDirX, rayDirY);

        const int lineH    = static_cast<int>(H / h.perp_dist);
        const int rawStart = -lineH / 2 + H / 2;
        int start = rawStart < 0 ? 0 : rawStart;
        int end   = lineH / 2 + H / 2; if (end >= H) end = H - 1;

        const gfx::Image& tex = textures_.for_id(h.wall);
        const int texW = tex.w, texH = tex.h;

        int texX = static_cast<int>(h.wall_x * texW);
        if (h.side == 0 && rayDirX > 0) texX = texW - 1 - texX;  // keep texture orientation
        if (h.side == 1 && rayDirY < 0) texX = texW - 1 - texX;
        if (texX < 0) texX = 0; if (texX >= texW) texX = texW - 1;

        // Distance fog + side shading, applied per column (cheap).
        double shade = 1.0 - h.perp_dist / 14.0;
        if (shade < 0.35) shade = 0.35; if (shade > 1.0) shade = 1.0;
        if (h.side == 1) shade *= 0.78;

        const double step = static_cast<double>(texH) / (lineH > 0 ? lineH : 1);
        double texPos = static_cast<double>(start - rawStart) * step;

        const gfx::Color* px = tex.pixels.data();
        for (int y = start; y <= end; ++y) {
            int texY = static_cast<int>(texPos);
            texPos += step;
            if (texY < 0) texY = 0; if (texY >= texH) texY = texH - 1;
            const gfx::Color c = px[static_cast<size_t>(texY) * texW + texX];
            g.set_pixel(x, y, gfx::rgb(static_cast<uint8_t>(gfx::r_of(c) * shade),
                                       static_cast<uint8_t>(gfx::g_of(c) * shade),
                                       static_cast<uint8_t>(gfx::b_of(c) * shade)));
        }
    }

    g.draw_text(8, 8, "FPS RAYCASTER  -  WASD move, arrows turn, ESC quit",
                gfx::colors::white, 1);
}

} // namespace fps
