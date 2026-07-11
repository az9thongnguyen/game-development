// =============================================================================
//  games/editor/editor_scene.cpp  —  editor sandbox implementation
// =============================================================================
#include "games/editor/editor_scene.hpp"

#include <cmath>
#include <cstdio>

#include "engine/color.hpp"
#include "engine/ui/theme.hpp"

namespace editor {

using platform::MouseButton;

namespace {
// World uses pixel units (1 unit = 1 px); gravity in px/s².
constexpr float kGravity = 900.0f;
constexpr float kRadius  = 14.0f;

void fill_circle(gfx::Renderer2D& g, int cx, int cy, int r, gfx::Color c) {
    for (int dy = -r; dy <= r; ++dy) {
        const int dx = static_cast<int>(std::sqrt(static_cast<float>(r * r - dy * dy)));
        for (int x = cx - dx; x <= cx + dx; ++x) g.set_pixel(x, cy + dy, c);
    }
}
} // namespace

EditorScene::EditorScene() { reset_world(); }

void EditorScene::reset_world() {
    world_ = phys::World{};
    world_.set_gravity({0.0f, gravity_on_ ? kGravity : 0.0f});
    // Static geometry: floor + two side walls (thick boxes just off-screen).
    world_.add(phys::static_body({static_cast<float>(w_) / 2.0f, static_cast<float>(h_) - 16.0f},
                                 phys::box({static_cast<float>(w_), 16.0f}), 0.3f));   // floor
    world_.add(phys::static_body({0.0f, static_cast<float>(h_) / 2.0f},
                                 phys::box({16.0f, static_cast<float>(h_)}), 0.3f));    // left wall
    world_.add(phys::static_body({static_cast<float>(w_), static_cast<float>(h_) / 2.0f},
                                 phys::box({16.0f, static_cast<float>(h_)}), 0.3f));    // right wall
    static_count_ = world_.count();
}

void EditorScene::spawn_at(float x, float y) {
    const phys::Shape s = (spawn_ == Spawn::Circle) ? phys::circle(kRadius)
                                                    : phys::box({kRadius, kRadius});
    world_.add(phys::make_body({x, y}, s, 1.0f, restitution_));
}

void EditorScene::update(double dt, const platform::InputState& /*input*/) {
    world_.set_gravity({0.0f, gravity_on_ ? kGravity : 0.0f});
    world_.step(dt);
}

void EditorScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width();
    h_ = g.height();
    g.set_font(ctx.font, ui::theme::sz_body);   // AA UI text (8x8 fallback if null)
    if (ctx.dt > 0.0) fps_ = fps_ * 0.92 + (1.0 / ctx.dt) * 0.08;

    g.clear(gfx::rgb(28, 30, 40));

    // ---- draw bodies ----
    for (int i = 0; i < world_.count(); ++i) {
        const phys::Body& b  = world_.body(i);
        const bool        st = !b.dynamic();
        const int         x  = static_cast<int>(b.pos.x);
        const int         y  = static_cast<int>(b.pos.y);
        if (b.shape.type == phys::ShapeType::Circle) {
            fill_circle(g, x, y, static_cast<int>(b.shape.radius), gfx::rgb(90, 180, 235));
        } else {
            const int hw = static_cast<int>(b.shape.half.x), hh = static_cast<int>(b.shape.half.y);
            g.fill_rect(x - hw, y - hh, hw * 2, hh * 2,
                        st ? gfx::rgb(70, 74, 86) : gfx::rgb(230, 170, 70));
        }
    }

    // ---- UI (immediate mode: input + draw in one pass) ----
    ui::Input in;
    in.mx       = ctx.input.mouse_x;
    in.my       = ctx.input.mouse_y;
    in.down     = ctx.input.down(MouseButton::Left);
    in.pressed  = ctx.input.pressed(MouseButton::Left);
    in.released = ctx.input.released(MouseButton::Left);

    ui_.begin(&g, in);
    ui_.panel(ui::Rect{12, 12, 220, 252}, "EDITOR");
    char count[48];
    std::snprintf(count, sizeof(count), "bodies: %d   fps: %d",
                  world_.count() - static_count_, static_cast<int>(fps_ + 0.5));
    ui_.label(count);
    if (ui_.button("Spawn Circle")) { spawn_ = Spawn::Circle; spawn_at(static_cast<float>(w_) / 2, 60); }
    if (ui_.button("Spawn Box"))    { spawn_ = Spawn::Box;    spawn_at(static_cast<float>(w_) / 2, 60); }
    ui_.checkbox("gravity", gravity_on_);
    ui_.slider("restitution", restitution_, 0.0f, 1.0f);
    if (ui_.button("Reset")) reset_world();
    ui_.end();

    // ---- click in the world (not over the panel) drops a body ----
    // After end(): hovering_ui() reflects this frame's widgets; world-spawn uses the
    // PRESS edge while UI buttons act on RELEASE, so the two never fire on one click.
    if (ctx.input.pressed(MouseButton::Left) && !ui_.hovering_ui())
        spawn_at(static_cast<float>(in.mx), static_cast<float>(in.my));

    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(12, h_ - 22,
                "click the world to drop a body   -   spawn: current kind   -   ESC: quit",
                ui::theme::text_muted);
}

} // namespace editor
