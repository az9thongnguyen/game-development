// =============================================================================
//  games/light/light_scene.hpp  —  the 2D lighting playground (--light)
// =============================================================================
//  A dark tiled room lit by additive radial lights: two coloured lights (one
//  static, one drifting via a tween) plus a white light that follows the mouse.
//  The lighting *math* is the pure engine/fx/light core; this scene is only the
//  SDL-touching glue — it loops each light's bounding box and deposits the
//  contribution through Renderer2D::add_pixel. See docs/book/84-2d-lighting.md.
// =============================================================================
#pragma once
#include <vector>

#include "engine/anim/tween.hpp"
#include "engine/fx/light.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"

namespace lightdemo {

class LightScene : public engine::Scene {
public:
    LightScene();
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    std::vector<fx::Light> lights_;      // [0] warm static, [1] cool drifting, [2] mouse
    ui::Context            ui_;
    anim::Tween            drift_;       // ping-pong drive for lights_[1].x (reuses tween_core)
    float                  mx_ = 480, my_ = 300;
    float                  mouse_radius_ = 170;
    float                  mouse_intensity_ = 1.1f;
    int                    w_ = 960, h_ = 600;
};

} // namespace lightdemo
