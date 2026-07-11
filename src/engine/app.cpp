// =============================================================================
//  engine/app.cpp  —  fixed-timestep loop body
// =============================================================================
#include "engine/app.hpp"

#include <cstdio>

#include "engine/assets.hpp"
#include "platform/platform.hpp"

namespace engine {

App::App(std::unique_ptr<Scene> scene) : scene_(std::move(scene)) {
    // Load the shared UI face once. Resolved via the asset seam (base path is
    // "assets", so this is "assets/fonts/Inter.ttf" natively and the same path in
    // the web VFS). On failure the whole UI transparently falls back to the
    // embedded 8x8 font — never a crash.
    if (auto bytes = assets::load_file("fonts/Inter.ttf")) {
        ui_font_ = text::Font::load_from_bytes(std::move(*bytes));
    }
    if (!ui_font_) {
        std::fprintf(stderr, "app: UI font not loaded — using 8x8 fallback\n");
    }
}

void App::frame(double dt) {
    // Guard against the "spiral of death": if one frame took a long time (the
    // window was dragged, a breakpoint hit, the laptop slept), don't try to catch
    // up with hundreds of update steps at once — clamp the elapsed time.
    if (dt > 0.25) {
        dt = 0.25;
    }

    // Consume real time in fixed-size chunks. Logic therefore always sees the
    // SAME dt (kFixedDt), which keeps movement/physics/AI deterministic and
    // independent of the display's refresh rate.
    accumulator_ += dt;
    while (accumulator_ >= kFixedDt) {
        scene_->update(kFixedDt, platform::input());
        accumulator_ -= kFixedDt;
        time_        += kFixedDt;
    }

    // Render once, after the logic has caught up. `alpha` is how far we are into
    // the next not-yet-simulated step; scenes can use it to interpolate motion so
    // rendering looks smooth even though logic ticks at a fixed rate.
    gfx::Renderer2D renderer(platform::framebuffer(), platform::supersample());
    Context ctx{ renderer, platform::input(), dt, time_, accumulator_ / kFixedDt, ui_font_.get() };
    scene_->render(ctx);
}

} // namespace engine
