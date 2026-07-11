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
inline constexpr gfx::Color bg        = 0xFF12141C;   // window background
inline constexpr gfx::Color elevated  = 0xFF1B1E28;   // panels / cards
inline constexpr gfx::Color titlebar  = 0xFF232733;   // panel title strip
inline constexpr gfx::Color border    = 0xFF2A2E3A;   // hairline separators/outlines

// ---- Colour: controls (neutral, by interaction state) -----------------------
inline constexpr gfx::Color ctrl       = 0xFF2A2F3C;  // button idle
inline constexpr gfx::Color ctrl_hover = 0xFF353B4A;  // hovered
inline constexpr gfx::Color ctrl_press = 0xFF454C5E;  // pressed
inline constexpr gfx::Color ctrl_disabled = 0xFF1E222C;
inline constexpr gfx::Color track      = 0xFF20242E;  // slider groove

// ---- Colour: text (by emphasis) ---------------------------------------------
inline constexpr gfx::Color text       = 0xFFE6E9F0;  // primary
inline constexpr gfx::Color text_dim   = 0xFFA8AEBE;  // secondary
inline constexpr gfx::Color text_muted = 0xFF6B7180;  // hint / disabled
inline constexpr gfx::Color on_accent  = 0xFF0B1017;  // text/icon on an accent fill

// ---- Colour: ONE hot-action accent + semantic -------------------------------
inline constexpr gfx::Color accent       = 0xFF5AAAE6;
inline constexpr gfx::Color accent_hover = 0xFF82C8FF;
inline constexpr gfx::Color accent_press = 0xFF3E86BE;
inline constexpr gfx::Color success = 0xFF4CC38A;
inline constexpr gfx::Color warn    = 0xFFE5B454;
inline constexpr gfx::Color danger  = 0xFFE5657A;

// ---- Spacing scale (px, logical) --------------------------------------------
inline constexpr int space_xs = 4;
inline constexpr int space_sm = 8;
inline constexpr int space_md = 12;
inline constexpr int space_lg = 16;
inline constexpr int space_xl = 24;

// ---- Corner radius ----------------------------------------------------------
inline constexpr int radius_sm = 6;    // controls
inline constexpr int radius_md = 10;   // panels

// ---- Type scale (px, logical) -----------------------------------------------
inline constexpr int sz_caption = 12;
inline constexpr int sz_body    = 14;
inline constexpr int sz_label   = 16;
inline constexpr int sz_title   = 20;
inline constexpr int sz_display = 28;

// ---- Elevation: a soft drop shadow (offset + spread + base alpha) -----------
struct Shadow { int dx, dy, spread; std::uint8_t a; };
inline constexpr Shadow shadow_panel{0, 4, 10, 90};

} // namespace ui::theme
