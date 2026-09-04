// =============================================================================
//  engine/app.hpp  —  the App: owns the active scene + the fixed-timestep clock
// =============================================================================
//  The App is the bridge between the platform's per-frame callback and the scene.
//  platform::run() calls App::frame(dt) once per frame; App turns that variable
//  dt into a fixed number of logic updates plus exactly one render.
// =============================================================================
#pragma once

#include <memory>

#include "engine/fixed_step.hpp"
#include "engine/scene.hpp"
#include "engine/text/font.hpp"

namespace engine {

class App {
public:
    explicit App(std::unique_ptr<Scene> scene);

    // Called once per platform frame. `dt` is real seconds since the last frame.
    void frame(double dt);

    // Fixed simulation step: logic always advances in 1/60 s increments.
    static constexpr double kFixedDt = 1.0 / 60.0;

private:
    std::unique_ptr<Scene>      scene_;
    std::unique_ptr<text::Font> ui_font_;    // shared UI face, loaded once at startup
    // The accumulator moved into FixedStep so the Play viewport can drive a scene on
    // the same clock rather than on a second one that agrees most of the time.
    FixedStep clock_{kFixedDt};
};

} // namespace engine
