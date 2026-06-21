// =============================================================================
//  games/editor/editor_scene.hpp  —  the --editor capstone: UI + physics sandbox
// =============================================================================
//  Ties subsystem F (the immediate-mode GUI) to subsystem E (2D physics) and the 2D
//  renderer: a panel of widgets (spawn buttons, a gravity checkbox, a restitution
//  slider, reset) plus a click-to-drop-bodies world. Immediate-mode UI couples input
//  and drawing, so all interaction happens in render() (called once per frame); the
//  physics simulation advances in update() at the fixed step.
// =============================================================================
#pragma once

#include "engine/physics/world.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"

namespace editor {

class EditorScene : public engine::Scene {
public:
    EditorScene();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    enum class Spawn { Circle, Box };

    void reset_world();                 // rebuild static geometry (floor + walls)
    void spawn_at(float x, float y);    // drop a body of the current kind

    ui::Context  ui_;
    phys::World  world_;
    int          static_count_ = 0;     // floor + walls (kept on reset)

    Spawn  spawn_     = Spawn::Circle;
    bool   gravity_on_ = true;
    float  restitution_ = 0.3f;

    int    w_ = 960, h_ = 600;
    double fps_ = 60.0;
};

} // namespace editor
