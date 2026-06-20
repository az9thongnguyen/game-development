// =============================================================================
//  main.cpp  —  engine entry point
// =============================================================================
//  Step 6: respond to the player. The scene moves a sprite with the keyboard
//  (arrows / WASD) in fixed-timestep update(), and draws a crosshair at the
//  mouse in render(). All input comes through the engine (platform::input via
//  Context), never SDL. Step 8 replaces this with the full M0 demo.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "engine/app.hpp"
#include "engine/color.hpp"
#include "engine/renderer2d.hpp"
#include "platform/platform.hpp"

namespace {

class InputDemoScene : public engine::Scene {
public:
    InputDemoScene() {
        // Amber diamond sprite with soft (alpha-faded) edges.
        sprite_pixels_.resize(16 * 16);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const int d = std::abs(x - 8) + std::abs(y - 8);
                const uint8_t a = d < 8 ? static_cast<uint8_t>(255 - d * 30) : 0;
                sprite_pixels_[y * 16 + x] = gfx::rgba(255, 210, 60, a);
            }
        }
        sprite_ = gfx::Sprite{ sprite_pixels_.data(), 16, 16 };
    }

    void update(double dt, const platform::InputState& in) override {
        using K = platform::Key;
        float dx = 0.0f, dy = 0.0f;
        if (in.down(K::Left)  || in.down(K::A)) dx -= 1.0f;
        if (in.down(K::Right) || in.down(K::D)) dx += 1.0f;
        if (in.down(K::Up)    || in.down(K::W)) dy -= 1.0f;
        if (in.down(K::Down)  || in.down(K::S)) dy += 1.0f;

        const float speed = 120.0f;  // pixels per second
        px_ += dx * speed * static_cast<float>(dt);
        py_ += dy * speed * static_cast<float>(dt);

        // Keep the sprite on screen.
        px_ = px_ < 0 ? 0 : (px_ > 480 ? 480 : px_);
        py_ = py_ < 0 ? 0 : (py_ > 270 ? 270 : py_);
    }

    void render(const engine::Context& ctx) override {
        gfx::Renderer2D& g            = ctx.gfx;
        const platform::InputState& in = ctx.input;

        g.clear(gfx::rgb(20, 24, 40));
        g.draw_rect(0, 0, g.width(), g.height(), gfx::rgb(40, 48, 72));

        g.blit(sprite_, int(px_) - 8, int(py_) - 8);  // player

        // Mouse crosshair (and a marker while the left button is held).
        g.draw_line(in.mouse_x - 5, in.mouse_y, in.mouse_x + 5, in.mouse_y, gfx::colors::white);
        g.draw_line(in.mouse_x, in.mouse_y - 5, in.mouse_x, in.mouse_y + 5, gfx::colors::white);
        if (in.down(platform::MouseButton::Left)) {
            g.fill_rect(in.mouse_x - 3, in.mouse_y - 3, 6, 6, gfx::rgba(255, 80, 80, 160));
        }

        char hud[96];
        std::snprintf(hud, sizeof hud, "mouse %d,%d   pos %d,%d",
                      in.mouse_x, in.mouse_y, int(px_), int(py_));
        g.draw_text(8, 8,  "ARROWS/WASD: MOVE   MOUSE: AIM   ESC: QUIT", gfx::colors::white, 1);
        g.draw_text(8, 18, hud, gfx::rgb(160, 170, 190), 1);
    }

private:
    std::vector<gfx::Color> sprite_pixels_;
    gfx::Sprite             sprite_{};
    float                   px_ = 240.0f, py_ = 135.0f;
};

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    platform::Config cfg;
    cfg.title     = "hand-engine — M0";
    cfg.fb_width  = 480;
    cfg.fb_height = 270;
    cfg.scale     = 2;

    if (!platform::init(cfg)) {
        return 1;
    }

    engine::App app(std::make_unique<InputDemoScene>());
    platform::run([&app](double dt) { app.frame(dt); });

    platform::shutdown();
    return 0;
}
