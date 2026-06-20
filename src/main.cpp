// =============================================================================
//  main.cpp  —  engine entry point
// =============================================================================
//  Step 5: exercise the 2D software renderer. The scene below clears the screen
//  and draws lines, rectangles, an alpha-blended sprite, and text — all through
//  our own renderer (no SDL drawing). Step 8 replaces it with the real M0 demo.
// =============================================================================
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#include "engine/app.hpp"
#include "engine/color.hpp"
#include "engine/renderer2d.hpp"
#include "platform/platform.hpp"

namespace {

class RendererTestScene : public engine::Scene {
public:
    RendererTestScene() {
        // Build a 16x16 sprite by hand: an amber diamond whose alpha fades out
        // toward the edges, so we can SEE alpha blending against the background.
        sprite_pixels_.resize(16 * 16);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const int d = std::abs(x - 8) + std::abs(y - 8);  // diamond metric
                const uint8_t a = d < 8 ? static_cast<uint8_t>(255 - d * 30) : 0;
                sprite_pixels_[y * 16 + x] = gfx::rgba(255, 210, 60, a);
            }
        }
        sprite_ = gfx::Sprite{ sprite_pixels_.data(), 16, 16 };
    }

    void render(const engine::Context& ctx) override {
        gfx::Renderer2D& g = ctx.gfx;
        g.clear(gfx::rgb(20, 24, 40));  // dark navy

        // A rotating fan of lines from the center (shows Bresenham in all octants).
        const int cx = g.width() / 2, cy = g.height() / 2;
        for (int i = 0; i < 12; ++i) {
            const float ang = float(i) / 12.0f * 6.2831853f + float(ctx.time) * 0.5f;
            const int x2 = cx + int(std::cos(ang) * 100.0f);
            const int y2 = cy + int(std::sin(ang) * 100.0f);
            g.draw_line(cx, cy, x2, y2, gfx::rgb(60, 120, 200));
        }

        // A filled rectangle with a white outline.
        g.fill_rect(20, 20, 60, 40, gfx::rgb(200, 60, 60));
        g.draw_rect(18, 18, 64, 44, gfx::colors::white);

        // The alpha sprite, orbiting the center.
        const int sx = cx + int(std::cos(ctx.time) * 80.0f) - 8;
        const int sy = cy + int(std::sin(ctx.time) * 80.0f) - 8;
        g.blit(sprite_, sx, sy);

        // Text at two scales.
        g.draw_text(20, g.height() - 40, "HAND-ENGINE M0", gfx::colors::white, 2);
        g.draw_text(20, g.height() - 16, "renderer2d: lines rects sprite text",
                    gfx::rgb(160, 170, 190), 1);
    }

private:
    std::vector<gfx::Color> sprite_pixels_;
    gfx::Sprite             sprite_{};
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

    engine::App app(std::make_unique<RendererTestScene>());
    platform::run([&app](double dt) { app.frame(dt); });

    platform::shutdown();
    return 0;
}
