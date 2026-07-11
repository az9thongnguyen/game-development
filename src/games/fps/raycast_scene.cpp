// =============================================================================
//  games/fps/raycast_scene.cpp
// =============================================================================
#include "games/fps/raycast_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "engine/assets.hpp"
#include "engine/color.hpp"
#include "engine/ui/theme.hpp"
#include "games/fps/raycast.hpp"

namespace fps {

namespace {
// Load a level authored in the Map/Level Lab (--maplab), falling back to the
// hand-built default_map() when the asset is absent or malformed.
Map load_level() {
    if (auto bytes = assets::load_file("maps/level_00.map")) {
        if (auto m = from_text(std::string(bytes->begin(), bytes->end()))) return *m;
    }
    return default_map();
}

// Start from the procedural wall textures, then skin wall ids 1..3 with any
// Texture Lab .hrt named textures/wall_<id>.hrt (the 3-tool join: Lab -> level
// walls). A missing/garbage file leaves that id on its hand-made procedural texture.
WallTextures load_wall_textures() {
    WallTextures wt = make_wall_textures();
    for (int id = 1; id <= 3; ++id) {
        char path[32]; std::snprintf(path, sizeof(path), "textures/wall_%d.hrt", id);
        if (auto img = gfx::load_image(path)) wt.tex[static_cast<size_t>(id)] = std::move(*img);
    }
    return wt;
}

// Multiply a color by a brightness factor, clamped to [0,255] per channel.
gfx::Color shade_color(gfx::Color c, double s) {
    auto ch = [&](uint8_t v) -> uint8_t {
        const double r = v * s;
        return static_cast<uint8_t>(r < 0.0 ? 0.0 : (r > 255.0 ? 255.0 : r));
    };
    return gfx::rgb(ch(gfx::r_of(c)), ch(gfx::g_of(c)), ch(gfx::b_of(c)));
}
} // namespace

RaycastScene::RaycastScene()
    : map_(load_level()),
      textures_(load_wall_textures()),
      barrel_(make_barrel()),
      sprites_{ {4.5, 8.5}, {3.5, 3.5}, {12.5, 3.5}, {3.5, 12.5}, {12.5, 12.5} },
      posX_(3.5), posY_(8.5),
      dirX_(1.0), dirY_(0.0),
      planeX_(0.0), planeY_(0.66)
{
    // Generate SFX once (signed 16-bit mono, assume 44.1 kHz device).
    const int rate = 44100;
    const int gn = rate * 18 / 100;  // gunshot: ~0.18s noise burst, fast decay
    gun_.resize(static_cast<size_t>(gn));
    uint32_t seed = 0x13572468u;
    for (int i = 0; i < gn; ++i) {
        seed = seed * 1664525u + 1013904223u;  // LCG noise
        const double noise = static_cast<double>((seed >> 9) & 0xFFFF) / 32768.0 - 1.0;
        const double t = i / static_cast<double>(gn);
        const double env = std::exp(-t * 16.0);
        gun_[static_cast<size_t>(i)] = static_cast<int16_t>(noise * env * 0.45 * 32767.0);
    }
    const int sn = rate * 9 / 100;   // footstep: ~0.09s low thump
    step_.resize(static_cast<size_t>(sn));
    for (int i = 0; i < sn; ++i) {
        const double t = i / static_cast<double>(sn);
        const double f = 110.0 * (1.0 - 0.4 * t);
        const double env = std::exp(-t * 22.0);
        const double s = std::sin(2.0 * 3.14159265 * f * i / rate);
        step_[static_cast<size_t>(i)] = static_cast<int16_t>(s * env * 0.5 * 32767.0);
    }
}

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

    // Shoot on Space (latched: one shot per press).
    if (in.down(K::Space) && !space_latched_) {
        space_latched_ = true;
        platform::play_sound(gun_.data(), static_cast<int>(gun_.size()));
    }
    if (!in.down(K::Space)) space_latched_ = false;

    // Footsteps while moving.
    const bool moving = in.down(K::W) || in.down(K::S) || in.down(K::A) || in.down(K::D) ||
                        in.down(K::Up) || in.down(K::Down);
    if (moving) {
        step_timer_ += dt;
        if (step_timer_ > 0.45) {
            platform::play_sound(step_.data(), static_cast<int>(step_.size()));
            step_timer_ = 0.0;
        }
    } else {
        step_timer_ = 0.0;
    }
}

void RaycastScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const int W = g.width(), H = g.height();
    if (static_cast<int>(zbuf_.size()) != W) zbuf_.assign(W, 0.0);

    // Sky/ground gradients toward the horizon give depth (walls already fog with
    // distance below), so the far plane reads as receding rather than a flat band.
    g.fill_v_gradient(0, 0,     W, H / 2,     gfx::rgb(46, 52, 70), gfx::rgb(20, 22, 30));  // ceiling
    g.fill_v_gradient(0, H / 2, W, H - H / 2, gfx::rgb(22, 22, 24), gfx::rgb(44, 41, 37));  // floor

    // ---- walls (also records per-column depth for sprite occlusion) ----
    for (int x = 0; x < W; ++x) {
        const double cameraX = 2.0 * x / W - 1.0;
        const double rayDirX = dirX_ + planeX_ * cameraX;
        const double rayDirY = dirY_ + planeY_ * cameraX;
        const Hit h = cast_ray(map_, posX_, posY_, rayDirX, rayDirY);
        zbuf_[x] = h.perp_dist;

        // Defensive floor: casting H/perp_dist to int is UB if perp_dist were ever 0
        // (cast_ray already floors it, but don't rely on the producer's invariant).
        const double perp  = h.perp_dist > 0.001 ? h.perp_dist : 0.001;
        const int lineH    = static_cast<int>(H / perp);
        const int rawStart = -lineH / 2 + H / 2;
        int start = rawStart < 0 ? 0 : rawStart;
        int end   = lineH / 2 + H / 2; if (end >= H) end = H - 1;

        const gfx::Image& tex = textures_.for_id(h.wall);
        const int texW = tex.w, texH = tex.h;
        int texX = static_cast<int>(h.wall_x * texW);
        if (h.side == 0 && rayDirX > 0) texX = texW - 1 - texX;
        if (h.side == 1 && rayDirY < 0) texX = texW - 1 - texX;
        if (texX < 0) texX = 0; if (texX >= texW) texX = texW - 1;

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
            g.set_pixel(x, y, shade_color(c, shade));
        }
    }

    // ---- sprites (billboards), drawn far-to-near, clipped by the depth buffer ----
    const int n = static_cast<int>(sprites_.size());
    std::vector<int> order(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) order[static_cast<size_t>(i)] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double da = (sprites_[a].x - posX_) * (sprites_[a].x - posX_) +
                          (sprites_[a].y - posY_) * (sprites_[a].y - posY_);
        const double db = (sprites_[b].x - posX_) * (sprites_[b].x - posX_) +
                          (sprites_[b].y - posY_) * (sprites_[b].y - posY_);
        return da > db;  // farthest first
    });

    const gfx::Image& im = barrel_;
    for (int oi = 0; oi < n; ++oi) {
        const SpriteInst& s = sprites_[static_cast<size_t>(order[static_cast<size_t>(oi)])];
        const Cam2 c = project_sprite(dirX_, dirY_, planeX_, planeY_, s.x - posX_, s.y - posY_);
        if (c.ty <= 0.01) continue;  // behind the camera

        const int screenX  = static_cast<int>((W / 2.0) * (1.0 + c.tx / c.ty));
        const int spriteH  = std::abs(static_cast<int>(H / c.ty));
        const int spriteW  = spriteH;  // square sprite
        const int rawStartY = -spriteH / 2 + H / 2;
        const int rawStartX = -spriteW / 2 + screenX;
        int sy0 = rawStartY < 0 ? 0 : rawStartY;
        int sy1 = spriteH / 2 + H / 2; if (sy1 >= H) sy1 = H - 1;
        int sx0 = rawStartX < 0 ? 0 : rawStartX;
        int sx1 = spriteW / 2 + screenX; if (sx1 > W) sx1 = W;
        if (spriteW <= 0) continue;

        double shade = 1.0 - c.ty / 14.0;
        if (shade < 0.35) shade = 0.35; if (shade > 1.0) shade = 1.0;

        for (int stripe = sx0; stripe < sx1; ++stripe) {
            if (!(c.ty < zbuf_[static_cast<size_t>(stripe)])) continue;  // occluded by a wall
            const int texX = (stripe - rawStartX) * im.w / spriteW;
            if (texX < 0 || texX >= im.w) continue;
            for (int y = sy0; y <= sy1; ++y) {
                const int texY = (y - rawStartY) * im.h / spriteH;
                if (texY < 0 || texY >= im.h) continue;
                const gfx::Color col = im.pixels[static_cast<size_t>(texY) * im.w + texX];
                if (gfx::a_of(col) == 0) continue;  // transparent texel
                g.set_pixel(stripe, y, shade_color(col, shade));
            }
        }
    }

    g.set_font(ctx.font, ui::theme::sz_caption);
    g.draw_text(8, 8, "FPS RAYCASTER    WASD: move    arrows: turn    SPACE: shoot    ESC: quit",
                ui::theme::text_dim);
}

} // namespace fps
