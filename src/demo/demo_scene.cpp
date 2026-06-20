// =============================================================================
//  demo/demo_scene.cpp  —  implementation of the M0 acceptance demo
// =============================================================================
#include "demo/demo_scene.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/assets.hpp"

namespace demo {

DemoScene::DemoScene() {
    // A 16x16 amber diamond with alpha that fades toward the edges.
    sprite_.resize(16 * 16);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int d = std::abs(x - 8) + std::abs(y - 8);
            const uint8_t a = d < 9 ? static_cast<uint8_t>(255 - d * 28) : 0;
            sprite_[y * 16 + x] = gfx::rgba(255, 210, 70, a);
        }
    }
    sprite_view_ = gfx::Sprite{ sprite_.data(), 16, 16 };

    // Asset seam: best-effort load of a sample file.
    if (auto data = assets::load_file("hello.txt")) {
        std::string s(data->begin(), data->end());
        const auto nl = s.find('\n');
        asset_line_ = (nl == std::string::npos) ? s : s.substr(0, nl);
    } else {
        asset_line_ = "(assets/hello.txt not found - run from project root)";
    }
}

void DemoScene::update(double dt, const platform::InputState& in) {
    using K = platform::Key;
    float dx = 0.0f, dy = 0.0f;
    if (in.down(K::Left)  || in.down(K::A)) dx -= 1.0f;
    if (in.down(K::Right) || in.down(K::D)) dx += 1.0f;
    if (in.down(K::Up)    || in.down(K::W)) dy -= 1.0f;
    if (in.down(K::Down)  || in.down(K::S)) dy += 1.0f;

    const float speed = 130.0f;  // px/second
    px_ += dx * speed * static_cast<float>(dt);
    py_ += dy * speed * static_cast<float>(dt);
    px_ = px_ < 8   ? 8   : (px_ > 472 ? 472 : px_);
    py_ = py_ < 40  ? 40  : (py_ > 250 ? 250 : py_);
}

void DemoScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g             = ctx.gfx;
    const platform::InputState& in = ctx.input;
    const int W = g.width(), H = g.height();

    // Smooth the FPS so the number is readable (exponential moving average).
    if (ctx.dt > 0.0) {
        fps_ = fps_ * 0.9 + (1.0 / ctx.dt) * 0.1;
    }

    g.clear(gfx::rgb(16, 18, 28));

    // Faint reference grid (lots of draw_line calls — also a renderer stress test).
    for (int x = 0; x < W; x += 32) g.draw_line(x, 0, x, H, gfx::rgb(26, 30, 44));
    for (int y = 0; y < H; y += 32) g.draw_line(0, y, W, y, gfx::rgb(26, 30, 44));

    // A decorative sprite orbiting the center (uses simulated time).
    const int ox = W / 2 + int(std::cos(ctx.time * 0.9) * 70.0) - 8;
    const int oy = H / 2 + int(std::sin(ctx.time * 0.9) * 50.0) - 8;
    g.blit(sprite_view_, ox, oy);

    // The player sprite (input-driven).
    g.blit(sprite_view_, int(px_) - 8, int(py_) - 8);

    // Mouse crosshair.
    g.draw_line(in.mouse_x - 5, in.mouse_y, in.mouse_x + 5, in.mouse_y, gfx::colors::white);
    g.draw_line(in.mouse_x, in.mouse_y - 5, in.mouse_x, in.mouse_y + 5, gfx::colors::white);

    // Centered title.
    const char* title = "HAND-ENGINE  M0";
    const int tw = int(std::strlen(title)) * 8 * 2;  // scale 2 → 16px/glyph
    g.draw_text((W - tw) / 2, 6, title, gfx::colors::white, 2);

    // FPS counter, top-right.
    char fps[24];
    std::snprintf(fps, sizeof fps, "FPS %2.0f", fps_);
    const int fw = int(std::strlen(fps)) * 8;
    g.draw_text(W - fw - 4, 6, fps, gfx::rgb(120, 230, 120), 1);

    // HUD.
    g.draw_text(8, H - 22, "ARROWS/WASD: MOVE   MOUSE: AIM   ESC: QUIT",
                gfx::rgb(170, 180, 200), 1);
    g.draw_text(8, H - 12, asset_line_.c_str(), gfx::rgb(120, 200, 120), 1);
}

} // namespace demo
