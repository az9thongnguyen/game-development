// =============================================================================
//  games/sandbox/sandbox_scene.cpp
// =============================================================================
#include "games/sandbox/sandbox_scene.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/assets.hpp"
#include "engine/ui/theme.hpp"
#include "games/sandbox/serialize.hpp"

namespace sandbox {

using platform::Key;
using platform::MouseButton;

namespace {
const gfx::Color kSwatches[] = {
    gfx::rgb(230, 90, 80),  gfx::rgb(240, 200, 70), gfx::rgb(90, 200, 120),
    gfx::rgb(80, 160, 240), gfx::rgb(200, 120, 230), gfx::rgb(230, 230, 235),
};
constexpr int kSwatchCount = int(sizeof(kSwatches) / sizeof(kSwatches[0]));
const char* kSceneFile = "sandbox/scene.scene";
} // namespace

SandboxScene::SandboxScene() {
    // ---- the fixed palette (design spec §4.1) ----
    Archetype ball;  ball.name = "ball"; ball.round = true; ball.color = gfx::rgb(240, 200, 70);
    ball.w = ball.h = 26; ball.mover = true; ball.vx = 120; ball.vy = 90; ball.bouncer = true;

    Archetype spin;  spin.name = "spinner"; spin.color = gfx::rgb(90, 200, 120);
    spin.w = 40; spin.h = 14; spin.spinner = true; spin.omega = 2.5f;

    Archetype emit;  emit.name = "emitter"; emit.color = gfx::rgb(80, 160, 240); emit.w = emit.h = 22;

    Archetype coin;  coin.name = "coin"; coin.round = true; coin.color = gfx::rgb(240, 220, 120);
    coin.w = coin.h = 16; coin.tag = 1;

    Archetype sweep; sweep.name = "sweeper"; sweep.color = gfx::rgb(230, 90, 80);
    sweep.w = 30; sweep.h = 30; sweep.mover = true; sweep.vx = 100; sweep.vy = 60; sweep.bouncer = true;

    palette_ = {
        {"Ball",    ball,  Extra::None},
        {"Spinner", spin,  Extra::None},
        {"Emitter", emit,  Extra::Emitter},
        {"Coin",    coin,  Extra::None},
        {"Sweeper", sweep, Extra::Sweeper},
    };
}

void SandboxScene::place(int i, float x, float y) {
    const PaletteItem& it = palette_[size_t(i)];
    ecs::Entity e = world_.spawn(it.arch, x, y);
    if (it.extra == Extra::Emitter) {
        Archetype pellet; pellet.round = true; pellet.color = gfx::rgb(180, 210, 255);
        pellet.w = pellet.h = 8; pellet.mover = true; pellet.vy = 140; pellet.lifetime = true; pellet.ttl = 2.5f;
        Spawner sp; sp.interval = 0.6f; sp.proto = pellet;
        world_.reg.add<Spawner>(e, sp);
    } else if (it.extra == Extra::Sweeper) {
        OnOverlap o; o.other_tag = 1; o.action = Action::DestroyOther;
        world_.reg.add<OnOverlap>(e, o);
    }
    sel_ = e; has_sel_ = true;
}

bool SandboxScene::pick_at(float x, float y, ecs::Entity& out) const {
    World& mw = const_cast<World&>(world_);
    bool found = false;
    mw.reg.view<Transform2D, Body>([&](ecs::Entity e, Transform2D& t, Body& b) {
        const float hw = b.w * t.scale * 0.5f, hh = b.h * t.scale * 0.5f;
        if (x >= t.x - hw && x <= t.x + hw && y >= t.y - hh && y <= t.y + hh) {
            out = e; found = true;   // keep last = topmost drawn
        }
    });
    return found;
}

void SandboxScene::toggle_play() {
    if (!playing_) { snapshot_ = to_scene(world_); playing_ = true; has_sel_ = false; dragging_ = false; }
    else           { world_ = from_scene(snapshot_); playing_ = false; has_sel_ = false; }
}

void SandboxScene::save() const {
    const std::string s = to_scene(world_);
    assets::write_file(kSceneFile, std::vector<uint8_t>(s.begin(), s.end()));
}
void SandboxScene::load() {
    auto bytes = assets::load_file(kSceneFile);
    if (!bytes) return;
    world_ = from_scene(std::string(bytes->begin(), bytes->end()));
    has_sel_ = false; playing_ = false;
}

void SandboxScene::update(double dt, const platform::InputState&) {
    if (playing_) world_.tick(float(dt));   // fixed-step, deterministic
}

void SandboxScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width(); h_ = g.height();
    if (!inited_) { world_.bounds_w = float(w_); world_.bounds_h = float(h_); inited_ = true; }
    g.set_font(ctx.font, ui::theme::sz_body);
    g.clear(gfx::rgb(20, 22, 30));

    // ---- draw the world ----
    g.draw_rect(0, 0, int(world_.bounds_w), int(world_.bounds_h), gfx::rgb(50, 54, 66));
    world_.reg.view<Transform2D, Body, Sprite>([&](ecs::Entity e, Transform2D& t, Body& b, Sprite& s) {
        const int cx = int(t.x), cy = int(t.y);
        const int dw = int(b.w * t.scale), dh = int(b.h * t.scale);
        if (s.round) g.fill_circle(cx, cy, dw / 2, s.color);
        else         g.fill_rect(cx - dw / 2, cy - dh / 2, dw, dh, s.color);
        // orientation tick so rotation/spin is visible
        const int ex = cx + int(std::cos(t.rot) * dw * 0.5f);
        const int ey = cy + int(std::sin(t.rot) * dw * 0.5f);
        g.draw_line(cx, cy, ex, ey, gfx::rgb(20, 22, 30));
        if (has_sel_ && e == sel_)
            g.draw_rect(cx - dw / 2 - 2, cy - dh / 2 - 2, dw + 4, dh + 4, gfx::rgb(255, 240, 120));
    });

    // ---- UI pass ----
    ui::Input in;
    in.mx = ctx.input.mouse_x; in.my = ctx.input.mouse_y;
    in.down     = ctx.input.down(MouseButton::Left);
    in.pressed  = ctx.input.pressed(MouseButton::Left);
    in.released = ctx.input.released(MouseButton::Left);
    ui_.begin(&g, in);

    ui_.panel(ui::Rect{12, 12, 150, 260}, playing_ ? "PLAYING" : "PALETTE");
    if (ui_.button(playing_ ? "Stop" : "Play", true)) toggle_play();
    if (!playing_) {
        if (ui_.button(armed_ < 0 ? "[Select/Move]" : "Select/Move")) armed_ = -1;
        for (size_t i = 0; i < palette_.size(); ++i) {
            char lbl[32];
            std::snprintf(lbl, sizeof(lbl), "%s%s", armed_ == int(i) ? "> " : "", palette_[i].label);
            if (ui_.button(lbl)) armed_ = int(i);
        }
    }

    // ---- inspector (edit mode + a selection) ----
    if (!playing_ && has_sel_ && world_.reg.valid(sel_)) {
        ui_.panel(ui::Rect{w_ - 176, 12, 164, 240}, "INSPECTOR");
        if (Transform2D* t = world_.reg.get<Transform2D>(sel_)) {
            ui_.slider("rot",   t->rot,   -3.14159f, 3.14159f);
            ui_.slider("scale", t->scale, 0.3f, 3.0f);
        }
        bool bounce = world_.reg.has<Bouncer>(sel_);
        if (ui_.checkbox("bounce", bounce)) {
            if (bounce) world_.reg.add<Bouncer>(sel_, {});
            else        world_.reg.remove<Bouncer>(sel_);
        }
        bool moves = world_.reg.has<Mover>(sel_);
        if (ui_.checkbox("move", moves)) {
            if (moves) world_.reg.add<Mover>(sel_, {90, 60});
            else       world_.reg.remove<Mover>(sel_);
        }
        if (ui_.button("Recolor")) {
            color_idx_ = (color_idx_ + 1) % kSwatchCount;
            if (Sprite* s = world_.reg.get<Sprite>(sel_)) s->color = kSwatches[color_idx_];
        }
        if (ui_.button("Delete")) { world_.reg.destroy(sel_); has_sel_ = false; }
    } else if (!playing_) {
        ui_.panel(ui::Rect{w_ - 176, 12, 164, 70}, "INSPECTOR");
        ui_.label("(click an actor)");
    }

    ui_.end();

    // ---- canvas interaction (only when the UI didn't consume the mouse) ----
    if (!playing_ && !ui_.hovering_ui()) {
        const float mx = float(in.mx), my = float(in.my);
        if (in.pressed) {
            if (armed_ >= 0) {
                place(armed_, mx, my);
            } else {
                ecs::Entity hit;
                if (pick_at(mx, my, hit)) {
                    sel_ = hit; has_sel_ = true; dragging_ = true;
                    if (Transform2D* t = world_.reg.get<Transform2D>(hit)) { drag_dx_ = mx - t->x; drag_dy_ = my - t->y; }
                } else {
                    has_sel_ = false;
                }
            }
        }
        if (in.down && dragging_ && has_sel_ && world_.reg.valid(sel_)) {
            if (Transform2D* t = world_.reg.get<Transform2D>(sel_)) { t->x = mx - drag_dx_; t->y = my - drag_dy_; }
        }
    }
    if (in.released) dragging_ = false;

    // ---- save/load ----
    if (ctx.input.pressed(Key::F5)) save();
    if (ctx.input.pressed(Key::F9)) load();

    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(12, h_ - 22,
                "palette: click to arm, click canvas to place - Select/Move to drag - Play/Stop - F5 save / F9 load",
                ui::theme::text_muted);
}

} // namespace sandbox
