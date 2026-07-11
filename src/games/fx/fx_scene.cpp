// =============================================================================
//  games/fx/fx_scene.cpp
// =============================================================================
#include "games/fx/fx_scene.hpp"

#include <algorithm>
#include <cstdio>

#include "engine/ui/theme.hpp"

namespace fx {

using platform::MouseButton;

namespace {
// Draw a particle system: one alpha-faded blob per particle. Scene-side (the sim
// core carries no Renderer2D dependency).
void draw(gfx::Renderer2D& g, const ParticleSystem& s) {
    for (const Particle& p : s.particles())
        g.fill_circle(int(p.x), int(p.y), std::max(1, int(current_size(p))), current_color(p));
}
} // namespace

FxScene::FxScene() {
    EmitterConfig c;                 // default is already an upward warm→red fountain
    c.rate = 300; c.speed = 220; c.speed_var = 60; c.spread = 0.35f;
    c.life = 1.6f; c.gravity = 300; c.size0 = 5; c.size1 = 0;
    sys_.set_config(c);
    sweep_ = anim::Tween{0.0f, 1.0f, 2.4f, anim::Ease::SineInOut, /*pingpong=*/true};
}

void FxScene::update(double dt, const platform::InputState&) {
    sweep_.update(float(dt));
    const float ex = sweep_on_ ? 80.0f + sweep_.value() * float(w_ - 160)
                               : float(w_) * 0.5f;
    sys_.update(float(dt), ex, float(h_) - 30.0f, fountain_);
}

void FxScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width(); h_ = g.height();
    g.set_font(ctx.font, ui::theme::sz_body);
    g.clear(gfx::rgb(12, 12, 18));

    draw(g, sys_);

    // ---- UI pass ----
    ui::Input in;
    in.mx = ctx.input.mouse_x; in.my = ctx.input.mouse_y;
    in.down     = ctx.input.down(MouseButton::Left);
    in.pressed  = ctx.input.pressed(MouseButton::Left);
    in.released = ctx.input.released(MouseButton::Left);
    ui_.begin(&g, in);

    ui_.panel(ui::Rect{12, 12, 190, 242}, "PARTICLE FX");
    ui_.checkbox("fountain", fountain_);
    ui_.checkbox("sweep", sweep_on_);
    ui_.slider("gravity", sys_.config().gravity, -200.0f, 700.0f);
    ui_.slider("rate",    sys_.config().rate,       0.0f, 700.0f);
    ui_.slider("speed",   sys_.config().speed,      0.0f, 400.0f);
    ui_.slider("spread",  sys_.config().spread,     0.0f, 3.14159f);
    if (ui_.button("Clear")) sys_.clear();
    char cnt[48];
    std::snprintf(cnt, sizeof(cnt), "particles: %zu / %zu", sys_.alive(), sys_.capacity());
    ui_.label(cnt);
    ui_.end();

    // ---- click to burst (when the UI didn't consume the mouse) ----
    if (!ui_.hovering_ui() && in.pressed)
        sys_.emit_burst(80, float(in.mx), float(in.my));

    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(210, h_ - 20, "click to burst - tune the fountain with the sliders",
                ui::theme::text_muted);
}

} // namespace fx
