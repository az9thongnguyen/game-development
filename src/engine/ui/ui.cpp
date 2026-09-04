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

Id Context::id_of(const char* s) const {
    Id h = 2166136261u;                             // FNV-1a
    for (; s && *s; ++s) { h ^= static_cast<unsigned char>(*s); h *= 16777619u; }
    // Mix in the enclosing scope so the SAME label under two different parents gets
    // two different ids. Without this, "Delete" in row 3 and "Delete" in row 7 are
    // one widget and clicking either drives whichever drew last.
    h ^= id_scope_ + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h ? h : 1u;                              // never 0 (0 = "no id")
}

void Context::push_id(const char* s) {
    id_stack_.push_back(id_scope_);
    Id h = id_scope_ ? id_scope_ : 2166136261u;
    for (; s && *s; ++s) { h ^= static_cast<unsigned char>(*s); h *= 16777619u; }
    id_scope_ = h ? h : 1u;
}

void Context::push_id(int i) {
    id_stack_.push_back(id_scope_);
    Id h = id_scope_ ? id_scope_ : 2166136261u;
    for (int b = 0; b < 4; ++b) { h ^= static_cast<Id>((i >> (b * 8)) & 0xFF); h *= 16777619u; }
    id_scope_ = h ? h : 1u;
}

void Context::pop_id() {
    if (id_stack_.empty()) return;                  // unbalanced: ignore, do not corrupt
    id_scope_ = id_stack_.back();
    id_stack_.pop_back();
}

bool Context::point_in(Rect r) const {
    return in_.mx >= r.x && in_.mx < r.x + r.w && in_.my >= r.y && in_.my < r.y + r.h;
}

void Context::begin(gfx::Renderer2D* r, const Input& in) {
    r_        = r;
    in_       = in;
    hot_      = 0;          // recompute hovered widget each frame
    hovering_ = false;
    id_scope_ = 0;
    id_stack_.clear();
    tab_order_.clear();     // rebuilt in declaration order as widgets are called
    // active_ and focused_ persist across frames (a drag / the keyboard's place)
}

void Context::end() {
    if (!in_.down) active_ = 0;   // safety: nothing can be active with the button up

    // Tab order is declaration order — the order the caller wrote the widgets in,
    // which is the order a reader sees them. Resolving it at end() rather than
    // per-widget means focus moves exactly one step per press however many widgets
    // are declared after the focused one.
    if (!tab_order_.empty() && (in_.keys.tab || in_.keys.tab_back)) {
        std::size_t at = 0;
        bool found = false;
        for (std::size_t i = 0; i < tab_order_.size(); ++i)
            if (tab_order_[i] == focused_) { at = i; found = true; break; }

        const std::size_t n = tab_order_.size();
        if (!found) {
            focused_ = in_.keys.tab ? tab_order_.front() : tab_order_.back();
        } else if (in_.keys.tab) {
            focused_ = tab_order_[(at + 1) % n];
        } else {
            focused_ = tab_order_[(at + n - 1) % n];
        }
    }

    // A control that stopped being declared cannot keep the keyboard.
    if (focused_) {
        bool still_there = false;
        for (Id id : tab_order_) if (id == focused_) { still_there = true; break; }
        if (!still_there) focused_ = 0;
    }
}

// Shared hover/press/focus bookkeeping. Every focusable widget routes through this,
// so "how does a control become hot / active / focused" has exactly one answer.
bool Context::interact(Id id, Rect r, bool enabled) {
    if (!enabled) return false;
    tab_order_.push_back(id);

    const bool over = point_in(r);
    if (over) { hot_ = id; hovering_ = true; }

    bool activated = false;
    if (active_ == id) {
        if (in_.released) { activated = over; active_ = 0; }
    } else if (over && in_.pressed) {
        active_  = id;
        focused_ = id;      // clicking a control also gives it the keyboard
    }

    // Enter/Space on the focused control does the same thing a click does.
    if (focused_ == id && in_.keys.activate) activated = true;
    return activated;
}

void Context::focus_ring(Rect r, int radius) const {
    if (!r_) return;
    r_->draw_round_rect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, radius + 2, th::accent);
}

// ---- explicit-rect widgets --------------------------------------------------
bool Context::button(Rect r, const char* label, bool primary, bool enabled) {
    const Id   id      = id_of(label);
    const bool clicked = interact(id, r, enabled);

    if (r_) {
        if (enabled && focused_ == id) focus_ring(r, th::radius_sm);
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
    const Id   id      = id_of(label);
    const bool toggled = interact(id, r, /*enabled*/ true);
    if (toggled) value = !value;

    if (r_) {
        if (focused_ == id) focus_ring(Rect{r.x, r.y, r.h, r.h}, th::radius_sm - 2);
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
    const Id   id   = id_of(label);
    const bool over = point_in(r);
    tab_order_.push_back(id);
    if (over) { hot_ = id; hovering_ = true; }

    // A slider drags rather than clicks, so it does not go through interact():
    // it stays active while the button is HELD, not until release-over.
    if (active_ == id) {
        if (!in_.down) active_ = 0;
    } else if (over && in_.pressed) {
        active_  = id;
        focused_ = id;
    }

    bool changed = false;
    // Keyboard: arrows nudge by 1/50 of the range, so a slider is reachable without
    // a mouse like every other control.
    if (focused_ == id && (in_.keys.left || in_.keys.right) && hi > lo) {
        const float step = (hi - lo) / 50.0f;
        const float nv   = clampf(value + (in_.keys.right ? step : -step), lo, hi);
        if (nv != value) { value = nv; changed = true; }
    }
    if (active_ == id && in_.down && r.w > 0) {
        const float t  = clampf(static_cast<float>(in_.mx - r.x) / static_cast<float>(r.w), 0.0f, 1.0f);
        const float nv = lo + t * (hi - lo);
        if (nv != value) { value = nv; changed = true; }
    }

    if (r_) {
        if (focused_ == id) focus_ring(Rect{r.x, r.y - 2, r.w, r.h + 4}, th::radius_sm);
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
