// =============================================================================
//  games/viz3d/editor_scene.cpp  —  the interactive 3D sandbox
// =============================================================================
#include "games/viz3d/editor_scene.hpp"

#include <cstdio>

#include "engine/color.hpp"
#include "engine/pick.hpp"

namespace viz3d {

using platform::Key;
using platform::MouseButton;

namespace {
// Spawn-color palette (cycled per spawn).
const gfx::Color kPalette[] = {
    gfx::rgb(230, 150, 60), gfx::rgb(90, 170, 230), gfx::rgb(120, 210, 120),
    gfx::rgb(220, 120, 200), gfx::rgb(230, 210, 90),
};
constexpr int kPaletteN = 5;
} // namespace

EditorScene::EditorScene()
    : cube_(geo::make_cube(kCubeSize)),
      sphere_(geo::make_sphere(kSphereRadius, 18, 28)),
      plane_(geo::make_plane(kPlaneSize, 1)),
      cylinder_(geo::make_cylinder(kCylRadius, kCylHeight, 28)),
      grid_(geo::make_grid(8.0f, 16, gfx::rgb(70, 74, 86))),
      axes_(geo::make_axes(2.8f)) {
    cam_.target = {0, 0.5f, 0};
    // Start with one cube so the sandbox isn't empty.
    editor_.spawn(Shape::Cube, {0, 0.5f, 0}, kPalette[spawn_count_++ % kPaletteN]);
}

geo::Mesh& EditorScene::mesh_for(Shape s) {
    switch (s) {
        case Shape::Cube:     return cube_;
        case Shape::Sphere:   return sphere_;
        case Shape::Plane:    return plane_;
        case Shape::Cylinder: return cylinder_;
    }
    return cube_;
}

cam::Ray EditorScene::mouse_ray(const platform::InputState& in) const {
    const float ndc_x = 2.0f * static_cast<float>(in.mouse_x) / static_cast<float>(w_) - 1.0f;
    const float ndc_y = 1.0f - 2.0f * static_cast<float>(in.mouse_y) / static_cast<float>(h_);
    return cam_.ray_through(ndc_x, ndc_y, static_cast<float>(w_) / static_cast<float>(h_));
}

void EditorScene::update(double dt, const platform::InputState& in) {
    const float fdt = static_cast<float>(dt);

    // ---- view toggles ----
    if (in.pressed(Key::Enter))
        mode_ = (mode_ == r3d::Mode::Wireframe)   ? r3d::Mode::SolidFlat
              : (mode_ == r3d::Mode::SolidFlat)   ? r3d::Mode::SolidGouraud
                                                  : r3d::Mode::Wireframe;
    if (in.pressed(Key::G)) show_grid_ = !show_grid_;
    if (in.pressed(Key::X)) show_axes_ = !show_axes_;
    if (in.pressed(Key::C)) cull_ = !cull_;

    // ---- spawn / delete / cycle / focus (mutate the editor first) ----
    auto spawn = [&](Shape s) { editor_.spawn(s, cam_.target, kPalette[spawn_count_++ % kPaletteN]); };
    if (in.pressed(Key::Num1)) spawn(Shape::Cube);
    if (in.pressed(Key::Num2)) spawn(Shape::Sphere);
    if (in.pressed(Key::Num3)) spawn(Shape::Plane);
    if (in.pressed(Key::Num4)) spawn(Shape::Cylinder);
    if (in.pressed(Key::Delete) || in.pressed(Key::Backspace)) editor_.remove_selected();
    if (in.pressed(Key::Tab)) editor_.cycle();
    if (in.pressed(Key::F)) { if (const Object* s = editor_.selected()) cam_.target = s->pos; }

    // ---- camera zoom (always) ----
    if (in.down(Key::W)) cam_.zoom(1.0f - 1.2f * fdt);
    if (in.down(Key::S)) cam_.zoom(1.0f + 1.2f * fdt);

    // ---- mouse ----
    const float dx = static_cast<float>(in.mouse_x - last_mx_);
    const float dy = static_cast<float>(in.mouse_y - last_my_);

    if (in.pressed(MouseButton::Left)) {
        const cam::Ray r = mouse_ray(in);
        const int hit = editor_.pick(r.origin, r.dir);
        editor_.select(hit);                       // -1 deselects on empty space
        left_drag_object_ = (hit >= 0);
    }
    if (in.down(MouseButton::Left)) {
        Object* sel = editor_.selected();
        if (left_drag_object_ && sel) {            // slide the object on its ground plane
            const cam::Ray r = mouse_ray(in);
            math::vec3 hitp;
            if (pick::ray_plane_y(r.origin, r.dir, sel->pos.y, hitp)) {
                sel->pos.x = hitp.x;
                sel->pos.z = hitp.z;
            }
        } else {                                   // orbit with empty-space left-drag
            cam_.orbit(-dx * 0.01f, dy * 0.01f);
        }
    } else {
        left_drag_object_ = false;                 // released: clear the drag flag
    }
    if (in.down(MouseButton::Right))  cam_.orbit(-dx * 0.01f, dy * 0.01f);
    if (in.down(MouseButton::Middle)) cam_.pan(dx, dy);

    last_mx_ = in.mouse_x;
    last_my_ = in.mouse_y;

    // ---- keyboard transform of the selected object (fetch fresh after mutations) ----
    if (Object* sel = editor_.selected()) {
        const float rot = 1.6f * fdt;
        if (in.down(Key::Left))  sel->yaw   += rot;
        if (in.down(Key::Right)) sel->yaw   -= rot;
        if (in.down(Key::Up))    sel->pitch += rot;
        if (in.down(Key::Down))  sel->pitch -= rot;
        if (in.down(Key::Q))     sel->pos.y -= 2.0f * fdt;
        if (in.down(Key::E))     sel->pos.y += 2.0f * fdt;
        if (in.down(Key::Minus))  sel->scale = math::clampf(sel->scale - 1.0f * fdt, 0.1f, 6.0f);
        if (in.down(Key::Equals)) sel->scale = math::clampf(sel->scale + 1.0f * fdt, 0.1f, 6.0f);
    } else {
        // Nothing selected: arrows orbit the camera.
        const float rot = 1.6f * fdt;
        if (in.down(Key::Left))  cam_.orbit(rot, 0);
        if (in.down(Key::Right)) cam_.orbit(-rot, 0);
        if (in.down(Key::Up))    cam_.orbit(0, rot);
        if (in.down(Key::Down))  cam_.orbit(0, -rot);
    }
}

void EditorScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width();
    h_ = g.height();
    const float aspect = static_cast<float>(w_) / static_cast<float>(h_);
    if (ctx.dt > 0.0) fps_ = fps_ * 0.92 + (1.0 / ctx.dt) * 0.08;

    r3_.begin(g, gfx::rgb(22, 24, 30));
    r3_.set_camera(cam_.view(), cam_.proj(aspect));
    r3_.set_cull(cull_);

    if (show_grid_) r3_.draw_lines(grid_, math::mat4_identity());
    if (show_axes_) r3_.draw_lines(axes_, math::mat4_identity());

    // Draw every object: recolor the shared template to the object's color, then draw.
    for (const Object& o : editor_.objects()) {
        geo::Mesh& m = mesh_for(o.shape);
        for (auto& v : m.vertices) v.color = o.color;
        r3_.draw_mesh(m, o.model(), mode_, light_);
    }
    // Selection highlight: yellow wire overlay on the selected object.
    if (const Object* sel = editor_.selected())
        r3_.draw_wire(mesh_for(sel->shape), sel->model(), gfx::colors::yellow);

    // ---- HUD ----
    const char* mname = (mode_ == r3d::Mode::Wireframe) ? "WIRE"
                      : (mode_ == r3d::Mode::SolidFlat)  ? "FLAT" : "GOURAUD";
    const Object* sel = editor_.selected();
    const char* sname = "none";
    if (sel) sname = (sel->shape == Shape::Cube) ? "cube"
                   : (sel->shape == Shape::Sphere) ? "sphere"
                   : (sel->shape == Shape::Plane) ? "plane" : "cylinder";
    char line[160];
    std::snprintf(line, sizeof(line), "objects:%d  sel:%s  scale:%.2f  mode:%s  cull:%s  fps:%d",
                  static_cast<int>(editor_.objects().size()), sname,
                  sel ? sel->scale : 0.0f, mname, cull_ ? "ON" : "OFF",
                  static_cast<int>(fps_ + 0.5));
    g.draw_text(8, 8,
        "VIZ3D  1-4:spawn  click:select  TAB:next  drag:move  QE:lift  arrows:rotate  -/=:scale  DEL:delete",
        gfx::colors::white, 1);
    g.draw_text(8, 22,
        "RMB:orbit  MMB:pan  W/S:zoom  F:focus  ENTER:shade  G/X/C:grid/axes/cull  ESC:quit",
        gfx::rgb(150, 160, 180), 1);
    g.draw_text(8, 40, line, gfx::rgb(180, 220, 255), 1);
}

} // namespace viz3d
