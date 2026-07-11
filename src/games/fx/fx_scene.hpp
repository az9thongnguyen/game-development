// =============================================================================
//  games/fx/fx_scene.hpp  —  the particle playground (--fx)
// =============================================================================
//  A tiny sandbox for engine/fx: a fountain emitter you can tune with sliders,
//  plus left-click bursts. The simulation runs in update() (fixed step,
//  deterministic); render() draws the particles and the UI. Only SDL-touching
//  particle file — the sim itself is the pure particles_core.
// =============================================================================
#pragma once
#include "engine/fx/particles.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"

namespace fx {

class FxScene : public engine::Scene {
public:
    FxScene();
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    ParticleSystem sys_;
    ui::Context    ui_;
    bool           fountain_ = true;
    int            w_ = 960, h_ = 600;   // seeded to the window size so frame-1 update is sane
};

} // namespace fx
