// =============================================================================
//  engine/ui/ui.cpp  —  immediate-mode GUI implementation
// =============================================================================
#include "engine/ui/ui.hpp"

#include <cstdio>

#include "engine/renderer2d.hpp"

namespace ui {
namespace {

// Theme.
constexpr gfx::Color kBase    = 0xFF3C4049;
constexpr gfx::Color kHover   = 0xFF505666;
constexpr gfx::Color kPressed = 0xFF6E7896;
constexpr gfx::Color kBorder  = 0xFF1E2028;
constexpr gfx::Color kTrack   = 0xFF282A34;
constexpr gfx::Color kAccent  = 0xFF5AAAE6;
constexpr gfx::Color kAccentH = 0xFF82C8FF;
constexpr gfx::Color kText    = 0xFFFFFFFF;
constexpr gfx::Color kPanelBg = 0xDC141620;   // translucent dark

constexpr int kRow = 22;
constexpr int kPad = 8;
constexpr int kGap = 6;

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
bool Context::button(Rect r, const char* label) {
    const std::uint32_t id = id_of(label);
    const bool over = point_in(r);
    if (over) { hot_ = id; hovering_ = true; }

    bool clicked = false;
    if (active_ == id) {
        if (in_.released) { clicked = over; active_ = 0; }
    } else if (over && in_.pressed) {
        active_ = id;
    }

    if (r_) {
        const gfx::Color bg = (active_ == id) ? kPressed : (hot_ == id ? kHover : kBase);
        r_->fill_rect(r.x, r.y, r.w, r.h, bg);
        r_->draw_rect(r.x, r.y, r.w, r.h, kBorder);
        r_->draw_text(r.x + 6, r.y + (r.h - 8) / 2, label, kText, 1);
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
        r_->fill_rect(r.x, r.y, s, s, hot_ == id ? kHover : kBase);
        r_->draw_rect(r.x, r.y, s, s, kBorder);
        if (value) r_->fill_rect(r.x + 4, r.y + 4, s - 8, s - 8, kAccent);
        r_->draw_text(r.x + s + 6, r.y + (s - 8) / 2, label, kText, 1);
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
        r_->fill_rect(r.x, r.y, r.w, r.h, kTrack);
        const float t  = (hi > lo) ? clampf((value - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
        const int   hx = r.x + static_cast<int>(t * static_cast<float>(r.w));
        r_->fill_rect(hx - 3, r.y - 2, 6, r.h + 4, (hot_ == id || active_ == id) ? kAccentH : kAccent);
        char buf[80];
        std::snprintf(buf, sizeof(buf), "%s: %.2f", label, static_cast<double>(value));
        r_->draw_text(r.x, r.y - 12, buf, kText, 1);
    }
    return changed;
}

void Context::label(int x, int y, const char* text, gfx::Color color) {
    if (r_) r_->draw_text(x, y, text, color, 1);
}

// ---- layout helpers ---------------------------------------------------------
void Context::panel(Rect bg, const char* title) {
    if (point_in(bg)) hovering_ = true;
    if (r_) {
        // translucent backdrop drawn pixel-by-pixel so it blends
        for (int yy = bg.y; yy < bg.y + bg.h; ++yy)
            for (int xx = bg.x; xx < bg.x + bg.w; ++xx)
                r_->blend_pixel(xx, yy, kPanelBg);
        r_->draw_rect(bg.x, bg.y, bg.w, bg.h, kBorder);
    }
    cx_ = bg.x + kPad;
    cw_ = bg.w - 2 * kPad;
    cy_ = bg.y + kPad;
    if (title) {
        if (r_) r_->draw_text(cx_, cy_, title, kAccentH, 1);
        cy_ += 16;
    }
}

bool Context::button(const char* label) {
    const Rect r{cx_, cy_, cw_, kRow};
    cy_ += kRow + kGap;
    return button(r, label);
}

bool Context::checkbox(const char* label, bool& value) {
    const Rect r{cx_, cy_, kRow, kRow};
    cy_ += kRow + kGap;
    return checkbox(r, label, value);
}

bool Context::slider(const char* label, float& value, float lo, float hi) {
    cy_ += 12;                                  // room for the label drawn above the track
    const Rect r{cx_, cy_, cw_, 10};
    cy_ += 10 + kGap;
    return slider(r, label, value, lo, hi);
}

void Context::label(const char* text) {
    label(cx_, cy_, text, kText);
    cy_ += kRow;
}

} // namespace ui
