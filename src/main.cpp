// =============================================================================
//  main.cpp  —  engine entry point
// =============================================================================
//  Step 3: build an engine::App around a Scene and let platform::run drive it.
//  The scene below is a throwaway that proves the fixed-timestep loop works by
//  animating the background color over time; Step 8 replaces it with the real
//  demo scene. Notice main() still only talks to the platform + engine APIs.
// =============================================================================
#include <cmath>
#include <cstdint>
#include <memory>

#include "engine/app.hpp"
#include "platform/platform.hpp"

namespace {

// Animate the whole screen through smoothly cycling colors. No game logic yet,
// so update() is empty and all the work happens in render().
class ColorScene : public engine::Scene {
public:
    void render(const engine::Context& ctx) override {
        // Three sine waves 120° apart give a smooth rainbow cycle over time.
        auto channel = [](double t, double phase) -> uint32_t {
            const double v = std::sin(t + phase) * 0.5 + 0.5;   // 0..1
            return static_cast<uint32_t>(v * 255.0) & 0xFFu;
        };
        const double t = ctx.time * 0.7;
        const uint32_t r = channel(t, 0.0);
        const uint32_t g = channel(t, 2.094);   // +120°
        const uint32_t b = channel(t, 4.188);   // +240°
        const uint32_t color = 0xFF000000u | (r << 16) | (g << 8) | b;

        const int count = ctx.fb.width * ctx.fb.height;
        for (int i = 0; i < count; ++i) {
            ctx.fb.pixels[i] = color;
        }
    }
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

    engine::App app(std::make_unique<ColorScene>());
    platform::run([&app](double dt) { app.frame(dt); });

    platform::shutdown();
    return 0;
}
