// =============================================================================
//  games/light/light_scene.cpp
// =============================================================================
#include "games/light/light_scene.hpp"

#include <algorithm>
#include <cstdio>

#include "engine/ui/theme.hpp"

namespace lightdemo {

namespace {
// Deposit every light additively. Scene-side (the light core carries no
// Renderer2D dependency): loop each light's bounding box, sample the pure
// falloff, add it. ponytail: naive O(radius^2) per light — fine for a handful;
// precompute a radial sprite and blit it if the light count ever grows large.
void draw_lights(gfx::Renderer2D& g, const std::vector<fx::Light>& ls) {
    const int W = g.width(), H = g.height();
    for (const fx::Light& L : ls) {
        const int x0 = std::max(0, int(L.x - L.radius));
        const int y0 = std::max(0, int(L.y - L.radius));
        const int x1 = std::min(W, int(L.x + L.radius) + 1);
        const int y1 = std::min(H, int(L.y + L.radius) + 1);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const gfx::Color c = fx::light_sample(L, float(x), float(y));
                if (gfx::a_of(c)) g.add_pixel(x, y, c);
            }
    }
}
} // namespace

LightScene::LightScene() {
    lights_.push_back(fx::Light{240, 380, 200, gfx::rgb(255, 170, 90),  1.0f});  // warm, static
    lights_.push_back(fx::Light{700, 220, 210, gfx::rgb(90, 160, 255),  1.0f});  // cool, drifts
    lights_.push_back(fx::Light{480, 300, 170, gfx::rgb(255, 250, 235), 1.1f});  // mouse light
    drift_ = anim::Tween{0.0f, 1.0f, 3.2f, anim::Ease::SineInOut, /*pingpong=*/true};
}

void LightScene::update(double dt, const platform::InputState& input) {
    drift_.update(float(dt));
    lights_[1].x = 120.0f + drift_.value() * float(w_ - 240);    // sweep the cool light
    mx_ = float(input.mouse_x);
    my_ = float(input.mouse_y);
    lights_[2].x = mx_; lights_[2].y = my_;
    lights_[2].radius = mouse_radius_;
    lights_[2].intensity = mouse_intensity_;
}

void LightScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width(); h_ = g.height();
    g.set_font(ctx.font, ui::theme::sz_body);

    // Dark room + a lattice of dim tiles for the lights to reveal.
    g.clear(gfx::rgb(10, 10, 16));
    for (int y = 0; y < h_; y += 48)
        for (int x = 0; x < w_; x += 48)
            g.fill_rect(x + 2, y + 2, 44, 44, gfx::rgb(38, 38, 52));

    draw_lights(g, lights_);

    // ---- UI pass (drawn on top so it stays readable) ----
    ui::Input in;
    in.mx = ctx.input.mouse_x; in.my = ctx.input.mouse_y;
    in.down     = ctx.input.down(platform::MouseButton::Left);
    in.pressed  = ctx.input.pressed(platform::MouseButton::Left);
    in.released = ctx.input.released(platform::MouseButton::Left);
    ui_.begin(&g, in);
    ui_.panel(ui::Rect{12, 12, 200, 150}, "2D LIGHTING");
    ui_.slider("radius",    mouse_radius_,    40.0f, 320.0f);
    ui_.slider("intensity", mouse_intensity_,  0.0f,   3.0f);
    char cnt[48];
    std::snprintf(cnt, sizeof(cnt), "lights: %zu (additive)", lights_.size());
    ui_.label(cnt);
    ui_.end();

    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(224, h_ - 20, "move the mouse — the white light follows; tune it with the sliders",
                ui::theme::text_muted);
}

} // namespace lightdemo
