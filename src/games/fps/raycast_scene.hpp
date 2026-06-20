// =============================================================================
//  games/fps/raycast_scene.hpp  —  first-person raycaster (engine::Scene)
// =============================================================================
//  Renders the grid Map as a pseudo-3D view by casting one ray per screen column
//  (DDA), and moves the player with WASD/arrows + grid collision. Built on the
//  engine: draws vertical wall strips with renderer2d, reads normalized input.
// =============================================================================
#pragma once

#include <vector>

#include "engine/scene.hpp"
#include "games/fps/map.hpp"
#include "games/fps/textures.hpp"

namespace fps {

struct SpriteInst { double x, y; };  // a billboard's world position

class RaycastScene : public engine::Scene {
public:
    RaycastScene();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    void try_move(double nx, double ny);  // move with axis-separated collision

    Map                     map_;
    WallTextures            textures_;
    gfx::Image              barrel_;
    std::vector<SpriteInst> sprites_;
    std::vector<double>     zbuf_;   // per-column wall depth (for sprite occlusion)
    // Lodev-style camera: position + direction + camera plane (perpendicular).
    double posX_, posY_;
    double dirX_, dirY_;
    double planeX_, planeY_;
};

} // namespace fps
