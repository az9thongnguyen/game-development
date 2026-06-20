// =============================================================================
//  demo/demo_scene.hpp  —  the M0 acceptance demo scene
// =============================================================================
//  Pulls every M0 subsystem together: fixed-timestep update, 2D software
//  rendering (grid, sprites, text), normalized input (move + aim), the asset
//  seam (loads a file), and a live, smoothed FPS counter. This is the scene the
//  M0 acceptance criteria are checked against.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "engine/color.hpp"
#include "engine/scene.hpp"

namespace demo {

class DemoScene : public engine::Scene {
public:
    DemoScene();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    std::vector<gfx::Color> sprite_;        // owns the sprite pixels
    gfx::Sprite             sprite_view_{};  // view used for blitting
    std::string             asset_line_;     // first line of assets/hello.txt
    float                   px_ = 240.0f, py_ = 170.0f;  // player position
    double                  fps_ = 60.0;     // exponentially-smoothed FPS
};

} // namespace demo
