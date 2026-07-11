// =============================================================================
//  games/anim/anim_scene.cpp
// =============================================================================
#include "games/anim/anim_scene.hpp"

#include <cstdio>
#include <utility>

#include "engine/ui/theme.hpp"

namespace animdemo {

using platform::MouseButton;

AnimScene::AnimScene() {
    // Prefer a Studio-exported sheet (ch.88); fall back to the built-in spinner.
    auto img = gfx::load_image("sprites/sheet_00.hrt");
    if (!img) img = gfx::load_image("sprites/spin_8.hrt");
    if (img) { sheet_ = std::move(*img); have_sheet_ = true; }
    // Frame count self-describes via aspect ratio — no hard-coded 8.
    frames_ = have_sheet_ ? anim::frames_in_sheet(sheet_.w, sheet_.h) : 8;
    fh_     = have_sheet_ && frames_ > 0 ? sheet_.h / frames_ : 48;
    fb_     = anim::Flipbook{frames_, fps_, loop_};
}

// A frame is a contiguous run of fh_ rows in the vertical sheet, so it's just a
// Sprite that points at the right offset — no copy, no sub-rect blit needed.
gfx::Sprite AnimScene::frame_sprite(int f) const {
    return gfx::Sprite{sheet_.pixels.data() + std::size_t(f) * fh_ * sheet_.w, sheet_.w, fh_};
}

void AnimScene::update(double dt, const platform::InputState&) {
    fb_.fps = fps_; fb_.loop = loop_;          // let the sliders drive the flipbook
    if (playing_) fb_.update(float(dt));
}

void AnimScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width(); h_ = g.height();
    g.set_font(ctx.font, ui::theme::sz_body);
    g.clear(gfx::rgb(24, 22, 32));

    if (have_sheet_) {
        const int cur = fb_.frame();

        // Big current frame (nearest-neighbour upscale — crisp retro pixels).
        const int big = 320, bx = w_ / 2 - big / 2, by = 70;
        g.draw_rect(bx - 1, by - 1, big + 2, big + 2, gfx::rgb(60, 58, 78));
        g.blit_scaled(frame_sprite(cur), bx, by, big, big);

        // The whole strip, active frame outlined.
        const int fs = 60, gap = 10;
        const int total = frames_ * (fs + gap) - gap;
        const int sx = (w_ - total) / 2, sy = by + big + 24;
        for (int f = 0; f < frames_; ++f) {
            const int x = sx + f * (fs + gap);
            g.blit_scaled(frame_sprite(f), x, sy, fs, fs);
            g.draw_rect(x, sy, fs, fs, gfx::rgb(50, 48, 64));
            if (f == cur) {
                g.draw_rect(x - 2, sy - 2, fs + 4, fs + 4, gfx::rgb(120, 230, 170));
                g.draw_rect(x - 1, sy - 1, fs + 2, fs + 2, gfx::rgb(120, 230, 170));
            }
        }
    } else {
        g.draw_text(30, h_ / 2, "asset missing: assets/sprites/spin_8.hrt", ui::theme::text_muted);
    }

    // ---- UI ----
    ui::Input in;
    in.mx = ctx.input.mouse_x; in.my = ctx.input.mouse_y;
    in.down     = ctx.input.down(MouseButton::Left);
    in.pressed  = ctx.input.pressed(MouseButton::Left);
    in.released = ctx.input.released(MouseButton::Left);
    ui_.begin(&g, in);
    ui_.panel(ui::Rect{12, 12, 210, 210}, "SPRITE ANIMATION");
    ui_.slider("fps", fps_, 1.0f, 24.0f);
    ui_.checkbox("loop", loop_);
    ui_.checkbox("playing", playing_);
    if (ui_.button("restart")) fb_.reset();
    char cnt[48];
    std::snprintf(cnt, sizeof(cnt), "frame: %d / %d%s",
                  have_sheet_ ? fb_.frame() : 0, frames_, fb_.done() ? "  (done)" : "");
    ui_.label(cnt);
    ui_.end();

    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(232, h_ - 20, "one Flipbook + a vertical sheet — drag fps, toggle loop, hit restart",
                ui::theme::text_muted);
}

} // namespace animdemo
