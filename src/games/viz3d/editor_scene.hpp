// =============================================================================
//  games/viz3d/editor_scene.hpp  —  the interactive 3D sandbox (M3.5)
// =============================================================================
//  Wires input -> the Editor model + the OrbitCamera + the M3 Renderer3D. The
//  model (editor.hpp) holds the data and the verbs; this scene is the thin shell
//  that maps mouse/keys to those verbs and draws the result. It touches no SDL —
//  it reads a normalized InputState and draws via Renderer3D, like every scene.
//
//  Controls: see docs/book/25, or the on-screen HUD.
// =============================================================================
#pragma once

#include "engine/camera.hpp"
#include "engine/geometry.hpp"
#include "engine/renderer3d.hpp"
#include "engine/scene.hpp"
#include "games/viz3d/editor.hpp"

namespace viz3d {

class EditorScene : public engine::Scene {
public:
    EditorScene();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    cam::Ray   mouse_ray(const platform::InputState& in) const;
    geo::Mesh& mesh_for(Shape s);

    r3d::Renderer3D  r3_;
    cam::OrbitCamera cam_;
    Editor           editor_;
    r3d::Light       light_;

    geo::Mesh cube_, sphere_, plane_, cylinder_, grid_, axes_;

    r3d::Mode mode_      = r3d::Mode::SolidFlat;
    bool      show_grid_ = true;
    bool      show_axes_ = true;
    bool      cull_      = true;

    int    spawn_count_ = 0;     // cycles the spawn color palette
    double fps_ = 60.0;
    int    w_ = 960, h_ = 600;   // framebuffer size (refreshed each render)

    // Mouse drag state.
    bool left_drag_object_ = false;  // left-drag moves the picked object, else orbits
    int  last_mx_ = 0, last_my_ = 0;
};

} // namespace viz3d
