// =============================================================================
//  games/viz3d/scene3d.hpp  —  the M3 acceptance scene (3D core showcase)
// =============================================================================
//  Renders a spinning cube + sphere above a ground grid with coordinate axes,
//  proving the 3D core end-to-end: transform pipeline, z-buffer depth, perspective,
//  flat/Gouraud/wireframe shading, backface culling, and both cameras. It is the
//  seed of the M3.5 `viz3d` interactive sandbox.
//
//  Controls:
//    left-drag      orbit (orbit cam) / look (fly cam)
//    W / S          zoom in / out      (orbit) | move fwd / back (fly)
//    A / D          —                  (orbit) | strafe          (fly)
//    arrows         orbit / pitch      (orbit) | look            (fly)
//    ENTER          cycle shading: wireframe -> flat -> Gouraud
//    SPACE          toggle backface culling
//    right-click    toggle camera: orbit <-> fly
//    ESC            quit
// =============================================================================
#pragma once

#include "engine/camera.hpp"
#include "engine/geometry.hpp"
#include "engine/renderer3d.hpp"
#include "engine/scene.hpp"

namespace viz3d {

class Scene3D : public engine::Scene {
public:
    Scene3D();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    r3d::Renderer3D  r3_;
    cam::OrbitCamera orbit_;
    cam::FlyCamera   fly_;
    r3d::Light       light_;

    geo::Mesh cube_;
    geo::Mesh sphere_;
    geo::Mesh floor_;
    geo::Mesh grid_;
    geo::Mesh axes_;

    r3d::Mode mode_   = r3d::Mode::SolidGouraud;   // smooth by default (ENTER cycles)
    bool      use_fly_ = false;
    bool      cull_    = true;
    double    spin_    = 0.0;
    double    fps_     = 60.0;

    // Left-drag tracking (framebuffer-space mouse).
    bool drag_ = false;
    int  last_mx_ = 0;
    int  last_my_ = 0;
};

} // namespace viz3d
