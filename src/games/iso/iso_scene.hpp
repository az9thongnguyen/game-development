// =============================================================================
//  games/iso/iso_scene.hpp  —  the interactive isometric farm sandbox (M4)
// =============================================================================
//  Thin shell that maps mouse/keyboard onto the Farm model + the 2D camera, then
//  draws via iso_render. It holds no game rules itself (those live in Farm); it
//  only translates input into Farm verbs and presents the result + a HUD. Touches
//  no SDL: reads a normalized InputState, draws through Renderer2D, saves/loads
//  through the assets seam. Controls: see the HUD or book ch31.
// =============================================================================
#pragma once

#include <string>

#include "engine/scene.hpp"
#include "games/iso/farm.hpp"
#include "games/iso/iso_render.hpp"

namespace iso {

enum class Brush {
    Grass, Soil, Water, Path,          // terrain (paint the floor)
    Tree, Rock, House, Fence, Wheat,   // objects (place on top)
    Bulldoze                           // remove an object
};

class IsoScene : public engine::Scene {
public:
    IsoScene();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    Vec2i hovered_cell(const platform::InputState& in) const;
    void  apply_brush(int gx, int gy);
    void  flash(const std::string& msg);

    Farm     farm_;
    Camera2D cam_;
    Brush    brush_   = Brush::Tree;
    Vec2i    hovered_ = {-1, -1};
    Vec2i    last_paint_ = {-9999, -9999};  // avoid re-placing on the same tile

    int    w_ = 960, h_ = 600;
    double fps_      = 60.0;
    std::string status_;
    double      status_t_ = 0.0;   // seconds left to show the status flash
};

} // namespace iso
