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
    // Consume real time in fixed-size chunks. Logic therefore always sees the SAME
    // dt (kFixedDt), which keeps movement/physics/AI deterministic and independent of
    // the display's refresh rate. The clamp against the "spiral of death" lives in
    // FixedStep, with the Play viewport on the other side of it.
    const int steps = clock_.advance(dt);
    for (int i = 0; i < steps; ++i) scene_->update(kFixedDt, platform::input());

    // Render once, after the logic has caught up. `alpha` is how far we are into
    // the next not-yet-simulated step; scenes can use it to interpolate motion so
    // rendering looks smooth even though logic ticks at a fixed rate.
    gfx::Renderer2D renderer(platform::framebuffer(), platform::supersample());
    Context ctx{ renderer, platform::input(), dt, clock_.time(), clock_.alpha(), ui_font_.get() };
    scene_->render(ctx);
}

} // namespace engine
