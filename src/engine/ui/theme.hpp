// =============================================================================
//  engine/ui/theme.hpp  —  the design-system tokens (single source of truth)
// =============================================================================
//  "Modern dark + one accent" (Linear/Material-like). Every widget draws from
//  THESE names, never from ad-hoc literals — that consistency (one accent, one
//  spacing scale, one type scale, one radius, one elevation) is what makes the UI
//  read as a single designed system instead of a pile of rectangles.
//
//  Header-only constexpr: zero runtime cost, usable from any widget.
// =============================================================================
#pragma once

#include <cstdint>

#include "engine/color.hpp"

namespace ui::theme {

// ---- Colour: surfaces (dark, low-chroma; higher = more "raised") ------------
inline constexpr gfx::Color bg        = 0xFF0B1020;   // window background
inline constexpr gfx::Color elevated  = 0xFF131B2D;   // panels / cards
inline constexpr gfx::Color titlebar  = 0xFF19243A;   // panel title strip
inline constexpr gfx::Color border    = 0xFF263550;   // hairline separators/outlines
inline constexpr gfx::Color surface_selected = 0xFF1B2944; // selected card / row

// ---- Colour: controls (neutral, by interaction state) -----------------------
inline constexpr gfx::Color ctrl       = 0xFF1E2A42;  // button idle
inline constexpr gfx::Color ctrl_hover = 0xFF293957;  // hovered
inline constexpr gfx::Color ctrl_press = 0xFF34476A;  // pressed
inline constexpr gfx::Color ctrl_disabled = 0xFF151D2E;
inline constexpr gfx::Color track      = 0xFF182238;  // slider groove

// ---- Colour: text (by emphasis) ---------------------------------------------
inline constexpr gfx::Color text       = 0xFFF2F5FC;  // primary
inline constexpr gfx::Color text_dim   = 0xFFADB8CE;  // secondary
inline constexpr gfx::Color text_muted = 0xFF71809B;  // hint / disabled
inline constexpr gfx::Color on_accent  = 0xFF091021;  // text/icon on an accent fill

// ---- Colour: ONE hot-action accent + semantic -------------------------------
inline constexpr gfx::Color accent       = 0xFF7C9CFF;
inline constexpr gfx::Color accent_hover = 0xFFA2B8FF;
inline constexpr gfx::Color accent_press = 0xFF5E7FE5;
inline constexpr gfx::Color success = 0xFF45D39A;
inline constexpr gfx::Color warn    = 0xFFF0B85A;
inline constexpr gfx::Color danger  = 0xFFEF6B7B;
inline constexpr gfx::Color danger_hover = 0xFFFF8996;
inline constexpr gfx::Color danger_press = 0xFFD94F63;
inline constexpr gfx::Color info    = 0xFF5CC8EC;

// Added, not renamed: nine scenes already draw from the names above, so a rename
// would be churn with no gain. These are the ones that were genuinely missing.
inline constexpr gfx::Color scrim         = 0x8C000000;   // black @ 55% behind a modal
inline constexpr gfx::Color border_strong = 0xFF405477;   // popup/input outline

// Channel identity. The release pipeline is the one place a colour means a NAME
// rather than a state, so these live beside the semantic colours and not inside it.
inline constexpr gfx::Color chan_dev     = 0xFF7C9CFF;
inline constexpr gfx::Color chan_preview = 0xFFC792EA;
inline constexpr gfx::Color chan_prod    = 0xFF4CC38A;

// Mix an opaque colour toward another (pct = how much of `fg`). Badges want a tinted
// background, and mixing gives a solid colour that fill_rect can take the fast path
// with rather than an alpha blend per pixel.
constexpr gfx::Color mix(gfx::Color fg, gfx::Color bg, int pct) {
    const auto ch = [&](int shift) {
        const int a = static_cast<int>((fg >> shift) & 0xFFu);
        const int b = static_cast<int>((bg >> shift) & 0xFFu);
        return static_cast<std::uint32_t>((a * pct + b * (100 - pct)) / 100) << shift;
    };
    return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

// ---- Spacing scale (px, logical) --------------------------------------------
inline constexpr int space_xs = 4;
inline constexpr int space_sm = 8;
inline constexpr int space_md = 12;
inline constexpr int space_lg = 16;
inline constexpr int space_xl = 24;

// ---- Corner radius ----------------------------------------------------------
inline constexpr int radius_sm = 6;    // controls
inline constexpr int radius_md = 10;   // panels
inline constexpr int radius_lg = 14;   // product cards / modals

// ---- Type scale (px, logical) -----------------------------------------------
inline constexpr int sz_caption = 12;
inline constexpr int sz_body    = 14;
inline constexpr int sz_label   = 16;
inline constexpr int sz_title   = 20;
inline constexpr int sz_display = 28;

// ---- Elevation: a soft drop shadow (offset + spread + base alpha) -----------
struct Shadow { int dx, dy, spread; std::uint8_t a; };
// Subtle + tight: reads as clean elevation even at ss=1 (few px for the gradient)
// and against a near-black background where a big halo would look muddy.
inline constexpr Shadow shadow_panel{0, 3, 5, 70};

} // namespace ui::theme
