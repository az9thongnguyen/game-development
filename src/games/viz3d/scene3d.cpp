// =============================================================================
//  games/viz3d/scene3d.cpp  —  the M3 acceptance scene
// =============================================================================
#include "games/viz3d/scene3d.hpp"

#include <cstdio>

#include "engine/color.hpp"
#include "engine/math.hpp"

namespace viz3d {

using platform::Key;
using platform::MouseButton;

namespace {
constexpr float kCubeX   = -2.2f;   // cube sits left of the origin
constexpr float kSphereX =  2.2f;   // sphere sits right
constexpr float kFloorY  = -1.5f;
} // namespace

Scene3D::Scene3D()
    : cube_(geo::make_cube(2.0f, gfx::rgb(230, 150, 60))),
      sphere_(geo::make_sphere(1.2f, 20, 32, gfx::rgb(90, 170, 230))),
      floor_(geo::make_plane(16.0f, 1, gfx::rgb(40, 44, 52))),
      grid_(geo::make_grid(8.0f, 16, gfx::rgb(70, 74, 86))),
      axes_(geo::make_axes(2.8f)) {}

void Scene3D::update(double dt, const platform::InputState& in) {
    spin_ += dt;

    // ---- toggles (edge-triggered) ----
    if (in.pressed(Key::Enter)) {
        mode_ = (mode_ == r3d::Mode::Wireframe)   ? r3d::Mode::SolidFlat
              : (mode_ == r3d::Mode::SolidFlat)   ? r3d::Mode::SolidGouraud
                                                  : r3d::Mode::Wireframe;
    }
    if (in.pressed(Key::Space))               cull_ = !cull_;
    if (in.pressed(MouseButton::Right))       use_fly_ = !use_fly_;

    // ---- left-drag delta (framebuffer pixels) ----
    float dx = 0.0f, dy = 0.0f;
    if (in.down(MouseButton::Left)) {
        if (drag_) { dx = float(in.mouse_x - last_mx_); dy = float(in.mouse_y - last_my_); }
        drag_ = true;
        last_mx_ = in.mouse_x;
        last_my_ = in.mouse_y;
    } else {
        drag_ = false;
    }

    const float rot = 1.8f * static_cast<float>(dt);
    const float mv  = 4.0f * static_cast<float>(dt);

    if (use_fly_) {
        if (in.down(Key::W)) fly_.move(mv, 0, 0);
        if (in.down(Key::S)) fly_.move(-mv, 0, 0);
        if (in.down(Key::A)) fly_.move(0, -mv, 0);
        if (in.down(Key::D)) fly_.move(0, mv, 0);
        if (in.down(Key::Up))    fly_.look(0, rot);
        if (in.down(Key::Down))  fly_.look(0, -rot);
        if (in.down(Key::Left))  fly_.look(rot, 0);
        if (in.down(Key::Right)) fly_.look(-rot, 0);
        if (dx != 0.0f || dy != 0.0f) fly_.look(-dx * 0.005f, -dy * 0.005f);
    } else {
        if (in.down(Key::W)) orbit_.zoom(1.0f - 1.5f * static_cast<float>(dt));  // closer
        if (in.down(Key::S)) orbit_.zoom(1.0f + 1.5f * static_cast<float>(dt));  // farther
        if (in.down(Key::Left))  orbit_.orbit(rot, 0);
        if (in.down(Key::Right)) orbit_.orbit(-rot, 0);
        if (in.down(Key::Up))    orbit_.orbit(0, rot);
        if (in.down(Key::Down))  orbit_.orbit(0, -rot);
        if (dx != 0.0f || dy != 0.0f) orbit_.orbit(-dx * 0.01f, dy * 0.01f);
    }
}

void Scene3D::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const int W = g.width(), H = g.height();
    const float aspect = static_cast<float>(W) / static_cast<float>(H);

    if (ctx.dt > 0.0) fps_ = fps_ * 0.92 + (1.0 / ctx.dt) * 0.08;

    const math::mat4 view = use_fly_ ? fly_.view() : orbit_.view();
    const math::mat4 proj = use_fly_ ? fly_.proj(aspect) : orbit_.proj(aspect);

    r3_.begin(g, gfx::rgb(22, 24, 30));      // dark sky
    r3_.set_camera(view, proj);
    r3_.set_cull(cull_);

    // Ground first: a solid floor (always lit so it reads as a surface), then the
    // grid + axes overlaid on top of it for orientation.
    const math::mat4 floorM = math::mat4_translate({0, kFloorY, 0});
    r3_.draw_mesh(floor_, floorM, r3d::Mode::SolidFlat, light_);
    r3_.draw_lines(grid_, math::mat4_translate({0, kFloorY + 0.01f, 0}));
    r3_.draw_lines(axes_, math::mat4_identity());

    // The two spinning solids, in the currently selected shading mode.
    const math::mat4 cubeM =
        math::mat4_translate({kCubeX, 0.2f, 0}) *
        math::mat4_rotate_y(static_cast<float>(spin_)) *
        math::mat4_rotate_x(static_cast<float>(spin_) * 0.5f);
    r3_.draw_mesh(cube_, cubeM, mode_, light_);

    const math::mat4 sphM =
        math::mat4_translate({kSphereX, 0.2f, 0}) *
        math::mat4_rotate_y(static_cast<float>(-spin_) * 0.6f);
    r3_.draw_mesh(sphere_, sphM, mode_, light_);

    // ---- HUD ----
    const char* mname = (mode_ == r3d::Mode::Wireframe) ? "WIRE"
                      : (mode_ == r3d::Mode::SolidFlat)  ? "FLAT" : "GOURAUD";
    char line[128];
    std::snprintf(line, sizeof(line), "mode:%s  cam:%s  cull:%s  fps:%d",
                  mname, use_fly_ ? "FLY" : "ORBIT", cull_ ? "ON" : "OFF",
                  static_cast<int>(fps_ + 0.5));
    g.draw_text(8, 8, "3D CORE  -  drag:orbit  W/S:zoom  ENTER:mode  SPACE:cull  RMB:camera  ESC:quit",
                gfx::colors::white, 1);
    g.draw_text(8, 22, line, gfx::rgb(180, 220, 255), 1);
}

} // namespace viz3d
