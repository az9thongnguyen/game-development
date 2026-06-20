// =============================================================================
//  engine/app.cpp  —  fixed-timestep loop body
// =============================================================================
#include "engine/app.hpp"

#include "platform/platform.hpp"

namespace engine {

App::App(std::unique_ptr<Scene> scene) : scene_(std::move(scene)) {}

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
    gfx::Renderer2D renderer(platform::framebuffer());
    Context ctx{ renderer, platform::input(), dt, time_, accumulator_ / kFixedDt };
    scene_->render(ctx);
}

} // namespace engine
