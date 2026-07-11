// =============================================================================
//  engine/ui/ui.cpp  —  immediate-mode GUI implementation (design-system styling)
// =============================================================================
//  Styling comes entirely from engine/ui/theme.hpp (colours, spacing, radius, type
//  scale) and is drawn with the anti-aliased primitives (rounded rects, circles,
//  soft shadow, font text). The hot/active interaction model is unchanged — the
//  headless tests (null renderer) still exercise exactly the same logic.
//
//  Text: widgets draw through the renderer's CURRENT font (set by the scene via
//  gfx.set_font). With no font set, draw_text falls back to the 8x8 bitmap, so the
//  UI still works — it just isn't anti-aliased.
// =============================================================================
#include "engine/ui/ui.hpp"

#include <cstdio>

#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"

namespace ui {
namespace {

namespace th = theme;

// Widget metrics (logical px). Row heights are sized for the label type scale.
constexpr int kBtnH = 28;
constexpr int kChkH = 20;
constexpr int kGap  = th::space_sm;   // vertical gap between stacked widgets

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

std::uint32_t Context::id_of(const char* s) {
    std::uint32_t h = 2166136261u;                 // FNV-1a
    for (; s && *s; ++s) { h ^= static_cast<unsigned char>(*s); h *= 16777619u; }
    return h ? h : 1u;                              // never 0 (0 = "no id")
}

bool Context::point_in(Rect r) const {
    return in_.mx >= r.x && in_.mx < r.x + r.w && in_.my >= r.y && in_.my < r.y + r.h;
}

void Context::begin(gfx::Renderer2D* r, const Input& in) {
    r_        = r;
    in_       = in;
    hot_      = 0;          // recompute hovered widget each frame
    hovering_ = false;
    // active_ persists across frames (a drag in progress)
}

void Context::end() {
    if (!in_.down) active_ = 0;   // safety: nothing can be active with the button up
}

// ---- explicit-rect widgets --------------------------------------------------
bool Context::button(Rect r, const char* label, bool primary, bool enabled) {
    const std::uint32_t id = id_of(label);
    const bool over = point_in(r);

    bool clicked = false;
    if (enabled) {
        if (over) { hot_ = id; hovering_ = true; }
        if (active_ == id) {
            if (in_.released) { clicked = over; active_ = 0; }
        } else if (over && in_.pressed) {
            active_ = id;
        }
    }

    if (r_) {
        gfx::Color bg;
        if (!enabled)     bg = th::ctrl_disabled;
        else if (primary) bg = (active_ == id) ? th::accent_press : (hot_ == id ? th::accent_hover : th::accent);
        else              bg = (active_ == id) ? th::ctrl_press   : (hot_ == id ? th::ctrl_hover   : th::ctrl);

        r_->fill_round_rect(r.x, r.y, r.w, r.h, th::radius_sm, bg);
        if (!primary || !enabled) r_->draw_round_rect(r.x, r.y, r.w, r.h, th::radius_sm, th::border);

        const gfx::Color fg = !enabled ? th::text_muted : (primary ? th::on_accent : th::text);
        r_->set_font_size(th::sz_label);
        const int tw = r_->text_width(label);
        r_->draw_text(r.x + (r.w - tw) / 2, r.y + (r.h - th::sz_label) / 2, label, fg);
    }
    return clicked;
}

bool Context::checkbox(Rect r, const char* label, bool& value) {
    const std::uint32_t id = id_of(label);
    const bool over = point_in(r);
    if (over) { hot_ = id; hovering_ = true; }

    bool toggled = false;
    if (active_ == id) {
        if (in_.released) { if (over) { value = !value; toggled = true; } active_ = 0; }
    } else if (over && in_.pressed) {
        active_ = id;
    }

    if (r_) {
        const int s = r.h;
        r_->fill_round_rect(r.x, r.y, s, s, th::radius_sm - 2, (hot_ == id) ? th::ctrl_hover : th::ctrl);
        r_->draw_round_rect(r.x, r.y, s, s, th::radius_sm - 2, th::border);
        if (value) r_->fill_round_rect(r.x + 4, r.y + 4, s - 8, s - 8, th::radius_sm - 3, th::accent);
        r_->set_font_size(th::sz_label);
        r_->draw_text(r.x + s + th::space_sm, r.y + (s - th::sz_label) / 2, label, th::text);
    }
    return toggled;
}

bool Context::slider(Rect r, const char* label, float& value, float lo, float hi) {
    const std::uint32_t id = id_of(label);
    const bool over = point_in(r);
    if (over) { hot_ = id; hovering_ = true; }

    if (active_ == id) {
        if (!in_.down) active_ = 0;
    } else if (over && in_.pressed) {
        active_ = id;
    }

    bool changed = false;
    if (active_ == id && in_.down && r.w > 0) {
        const float t  = clampf(static_cast<float>(in_.mx - r.x) / static_cast<float>(r.w), 0.0f, 1.0f);
        const float nv = lo + t * (hi - lo);
        if (nv != value) { value = nv; changed = true; }
    }

    if (r_) {
        const int cy = r.y + r.h / 2;
        r_->fill_round_rect(r.x, cy - 3, r.w, 6, 3, th::track);           // groove
        const float t  = (hi > lo) ? clampf((value - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
        const int   hx = r.x + static_cast<int>(t * static_cast<float>(r.w));
        r_->fill_round_rect(r.x, cy - 3, hx - r.x, 6, 3, th::accent);     // filled portion
        const int knob = r.h / 2 + 2;
        r_->fill_circle(hx, cy, knob, (hot_ == id || active_ == id) ? th::accent_hover : th::accent);
        r_->fill_circle(hx, cy, knob - 4, th::text);                     // knob highlight dot

        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s: %.2f", label, static_cast<double>(value));
        r_->set_font_size(th::sz_caption);
        r_->draw_text(r.x, r.y - th::sz_caption - 2, buf, th::text_dim);
    }
    return changed;
}

void Context::label(int x, int y, const char* text, gfx::Color color) {
    if (r_) { r_->set_font_size(th::sz_body); r_->draw_text(x, y, text, color); }
}

// ---- layout helpers ---------------------------------------------------------
void Context::panel(Rect bg, const char* title) {
    if (point_in(bg)) hovering_ = true;
    if (r_) {
        r_->drop_shadow(bg.x, bg.y, bg.w, bg.h, th::radius_md,
                        th::shadow_panel.dx, th::shadow_panel.dy, th::shadow_panel.spread,
                        gfx::rgba(0, 0, 0, th::shadow_panel.a));
        r_->fill_round_rect(bg.x, bg.y, bg.w, bg.h, th::radius_md, th::elevated);
        r_->draw_round_rect(bg.x, bg.y, bg.w, bg.h, th::radius_md, th::border);
    }
    cx_ = bg.x + th::space_md;
    cw_ = bg.w - 2 * th::space_md;
    cy_ = bg.y + th::space_md;
    if (title) {
        if (r_) {
            r_->set_font_size(th::sz_title);
            r_->draw_text(cx_, cy_, title, th::text);
            r_->fill_rect(cx_, cy_ + th::sz_title + 4, cw_, 1, th::border);   // divider
        }
        cy_ += th::sz_title + th::space_md;
    }
}

bool Context::button(const char* label, bool primary, bool enabled) {
    const Rect r{cx_, cy_, cw_, kBtnH};
    cy_ += kBtnH + kGap;
    return button(r, label, primary, enabled);
}

bool Context::checkbox(const char* label, bool& value) {
    const Rect r{cx_, cy_, kChkH, kChkH};
    cy_ += kChkH + kGap;
    return checkbox(r, label, value);
}

bool Context::slider(const char* label, float& value, float lo, float hi) {
    cy_ += th::sz_caption + 4;                       // room for the label above the groove
    const Rect r{cx_, cy_, cw_, 10};
    cy_ += 10 + kGap + 2;                            // + a little for the knob overhang
    return slider(r, label, value, lo, hi);
}

void Context::label(const char* text) {
    label(cx_, cy_, text, th::text);
    cy_ += th::sz_body + kGap;
}

} // namespace ui
