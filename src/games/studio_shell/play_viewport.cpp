// =============================================================================
//  games/studio_shell/play_viewport.cpp
// =============================================================================
#include "games/studio_shell/play_viewport.hpp"

#include <algorithm>

#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"

namespace studioshell {

namespace th = ui::theme;

namespace {

// A game that is focused still must not receive the Studio's own chords. Anything
// with Cmd/Ctrl held goes nowhere, and Escape is reserved for giving focus back —
// a game that swallowed it would trap the keyboard inside the viewport.
// "The game is receiving nothing." A default-constructed InputState is NOT that: its
// mouse sits at (0,0), which is a real position in the game's space — the top-left
// corner — so an unfocused game would be told the pointer is parked there forever.
platform::InputState nothing() {
    platform::InputState out{};
    out.mouse_x = out.mouse_y = -1;
    return out;
}

}  // namespace

// The shell's pointer, in the game's own space — or none.
//
// Chapter 115 handed the game no pointer at all, on the grounds that a viewport which
// lies about where the pointer is is worse than one that admits it has none. That was
// right while there was no way to CHECK the transform: the shell had never been
// clicked. It has now, so the honest thing is to do the arithmetic.
//
// The rect comes from the last draw(), so the pointer is one frame behind. That is
// what every embedded viewport has, and the alternative is computing the layout twice
// and keeping the two copies in agreement — the failure this project keeps finding.
platform::InputState PlayViewport::gate(const platform::InputState& in, bool focused) {
    if (!focused || in.mods.super || in.mods.ctrl) {
        // A chord belongs to the Studio. A game that could see Cmd+K would make the
        // palette unreachable while it had focus, which turns an embedded player into
        // a trap rather than a tool.
        grabbed_ = false;
        return nothing();
    }

    platform::InputState out = in;
    const int esc = static_cast<int>(platform::Key::Escape);
    out.key_down[esc] = out.key_pressed[esc] = out.key_released[esc] = out.key_repeat[esc] = false;

    bool any_down = false;
    for (int i = 0; i < static_cast<int>(platform::MouseButton::Count); ++i)
        any_down = any_down || in.mouse_down[i];

    const bool inside = shown_.w > 0 && shown_.h > 0 &&
                        in.mouse_x >= shown_.x && in.mouse_x < shown_.x + shown_.w &&
                        in.mouse_y >= shown_.y && in.mouse_y < shown_.y + shown_.h;

    // A press that started inside keeps the pointer until it is released, wherever it
    // goes. Without that, dragging off the picture would stop the game hearing the
    // release, and every drag would end with a button the game still thinks is held.
    grabbed_ = any_down && (grabbed_ || inside);

    if (!inside && !grabbed_) {
        out.mouse_x = out.mouse_y = -1;
        for (int i = 0; i < static_cast<int>(platform::MouseButton::Count); ++i)
            out.mouse_down[i] = out.mouse_pressed[i] = out.mouse_released[i] = false;
        return out;
    }

    // Mapped through the rect rather than a stored scale: the panel can be smaller
    // than one native frame, and then the blit is fitted rather than whole-numbered.
    out.mouse_x = (in.mouse_x - shown_.x) * w_ / shown_.w;
    out.mouse_y = (in.mouse_y - shown_.y) * h_ / shown_.h;
    return out;
}

engine::OpResult PlayViewport::start(const std::string& entry) {
    if (!factory_) return {false, "no scene factory wired (this build cannot play)"};
    if (entry.empty()) return {false, "project declares no entry"};

    PlayTarget t = factory_(entry);
    if (!t.scene) return {false, "cannot play entry '" + entry + "': no scene for it"};
    if (t.w <= 0 || t.h <= 0) return {false, "entry '" + entry + "' has no framebuffer size"};

    scene_ = std::move(t.scene);
    entry_ = entry;
    w_ = t.w; h_ = t.h;
    // Zero-filled: the first draw happens before the first render, and an
    // uninitialised buffer would show one frame of whatever memory was there.
    pixels_.assign(static_cast<std::size_t>(w_) * h_, 0xFF000000u);
    clock_.reset();
    steps_ = 0;
    paused_ = false;
    pending_step_ = false;
    return {true, "playing " + entry + "  " + std::to_string(w_) + "x" + std::to_string(h_)};
}

void PlayViewport::stop() {
    shown_   = ui::Rect{};
    grabbed_ = false;
    scene_.reset();
    entry_.clear();
    pixels_.clear();
    w_ = h_ = 0;
    steps_ = 0;
    clock_.reset();
    paused_ = false;
    pending_step_ = false;
}

void PlayViewport::update(double dt, const platform::InputState& in, bool focused) {
    if (!scene_) return;

    // Paused means the clock stops too, not just the scene: a paused game whose
    // simulated time kept climbing would jump on resume.
    if (paused_ && !pending_step_) return;

    if (pending_step_) {
        // Exactly one step, regardless of how much real time passed while the
        // operator was looking at the frame.
        pending_step_ = false;
        clock_.advance(clock_.dt());
        scene_->update(clock_.dt(), gate(in, focused));
        ++steps_;
        return;
    }

    const int n = clock_.advance(dt);
    const platform::InputState gated = gate(in, focused);
    for (int i = 0; i < n; ++i) {
        scene_->update(clock_.dt(), gated);
        ++steps_;
    }
}

ui::Rect PlayViewport::draw(gfx::Renderer2D& g, ui::Rect area, text::Font* font, double real_dt) {
    if (!scene_ || w_ <= 0 || h_ <= 0) return ui::Rect{area.x, area.y, 0, 0};

    // Render the game into its own framebuffer at native size, ss=1. The real window
    // may supersample; here the frame is nearest-scaled up afterwards, so SSAA would
    // cost 4x the fill to be thrown away by the upscale.
    {
        platform::Framebuffer fb{pixels_.data(), w_, h_, w_};
        gfx::Renderer2D sub(fb, 1);
        if (font) sub.set_font(font, th::sz_body);
        // A fresh InputState: render() must not depend on input, and handing it the
        // shell's would be handing it coordinates from another space.
        const platform::InputState none{};
        const engine::Context ctx{sub, none, real_dt, clock_.time(), clock_.alpha(), font};
        scene_->render(ctx);
    }

    // Letterbox at a WHOLE-NUMBER scale: a pixel game at 1.37x has rows of different
    // heights, which is visible as banding on exactly the art it is meant to show.
    int scale = std::min(area.w / w_, area.h / h_);
    int dw, dh;
    if (scale >= 1) {
        dw = w_ * scale; dh = h_ * scale;
    } else {
        // The panel is smaller than one native frame; fit is better than clipping.
        const double s = std::min(static_cast<double>(area.w) / w_,
                                  static_cast<double>(area.h) / h_);
        dw = std::max(1, static_cast<int>(w_ * s));
        dh = std::max(1, static_cast<int>(h_ * s));
    }
    const ui::Rect dst{area.x + (area.w - dw) / 2, area.y + (area.h - dh) / 2, dw, dh};

    // The letterbox bars are drawn, not left transparent: the area behind is the
    // Studio's own background, and a game that appears to bleed into the chrome
    // makes it unclear where the game ends.
    g.fill_rect(area.x, area.y, area.w, area.h, th::bg);
    const gfx::Sprite s{pixels_.data(), w_, h_};
    g.blit_scaled(s, dst.x, dst.y, dst.w, dst.h);
    g.draw_rect(dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2, th::border);
    shown_ = dst;   // what the next update() maps the pointer through
    return dst;
}

} // namespace studioshell
