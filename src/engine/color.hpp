// =============================================================================
//  engine/color.hpp  —  ARGB8888 color helpers
// =============================================================================
//  A Color is just a 32-bit integer (the framebuffer's pixel format). These
//  helpers pack/unpack channels and do source-over alpha blending, so the rest
//  of the renderer never fiddles with shifts and masks inline.
// =============================================================================
#pragma once

#include <cstdint>

namespace gfx {

using Color = uint32_t;  // 0xAARRGGBB

inline constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (Color(a) << 24) | (Color(r) << 16) | (Color(g) << 8) | Color(b);
}
inline constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) { return rgba(r, g, b, 255); }

inline constexpr uint8_t a_of(Color c) { return (c >> 24) & 0xFF; }
inline constexpr uint8_t r_of(Color c) { return (c >> 16) & 0xFF; }
inline constexpr uint8_t g_of(Color c) { return (c >> 8)  & 0xFF; }
inline constexpr uint8_t b_of(Color c) { return  c        & 0xFF; }

// Source-over blend: draw `src` (with its alpha) on top of opaque `dst`.
// The "+127)/255" is rounding integer division. Result is opaque (framebuffer).
inline Color blend(Color dst, Color src) {
    const uint32_t sa = a_of(src);
    if (sa == 255) return src;        // common fast paths
    if (sa == 0)   return dst;
    const uint32_t ia = 255 - sa;
    const uint32_t r = (r_of(src) * sa + r_of(dst) * ia + 127) / 255;
    const uint32_t g = (g_of(src) * sa + g_of(dst) * ia + 127) / 255;
    const uint32_t b = (b_of(src) * sa + b_of(dst) * ia + 127) / 255;
    return rgba(uint8_t(r), uint8_t(g), uint8_t(b), 255);
}

namespace colors {
inline constexpr Color black      = 0xFF000000;
inline constexpr Color white      = 0xFFFFFFFF;
inline constexpr Color red        = 0xFFFF0000;
inline constexpr Color green      = 0xFF00FF00;
inline constexpr Color blue       = 0xFF0000FF;
inline constexpr Color yellow     = 0xFFFFFF00;
inline constexpr Color cyan       = 0xFF00FFFF;
inline constexpr Color magenta    = 0xFFFF00FF;
inline constexpr Color cornflower = 0xFF6495ED;
} // namespace colors

} // namespace gfx
