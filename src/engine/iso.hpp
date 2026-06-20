// =============================================================================
//  engine/iso.hpp  —  isometric projection math (pure, header-only)
// =============================================================================
//  Isometric ("iso") rendering is NOT 3D. It is a flat 2D drawing trick: we draw
//  a square tile grid as a lattice of 2:1 diamonds, so the world *looks* like it
//  is seen from a 3/4 overhead angle. Everything still lands in the same 2D
//  framebuffer — no z-buffer, no perspective divide. The only "depth" is the
//  ORDER we paint things in (see depth_key + the painter's algorithm in
//  iso_render). This header holds the two coordinate transforms (grid<->screen)
//  and the depth key; it is SDL-free and render-free so tests use it directly.
//
//  The diamond. A tile is TILE_W wide and TILE_H tall (TILE_W = 2*TILE_H is the
//  classic "2:1 iso"). Tile (gx,gy)'s CENTER on screen is:
//
//        +x grid →                 sx = ox + (gx - gy) * (TILE_W/2)
//        ┌─────┐                   sy = oy + (gx + gy) * (TILE_H/2)
//      ↙ │ 0,0 │ ↘
//   gy   └─────┘   gx     Moving +gx goes screen down-right; +gy goes down-left.
//      (down-left)(down-right)     (ox,oy) is the camera pan offset in pixels.
// =============================================================================
#pragma once

#include <cmath>

namespace iso {

// Tile footprint in screen pixels. 2:1 ratio = the canonical isometric look.
inline constexpr int kTileW = 64;
inline constexpr int kTileH = 32;

// Integer grid coordinate. Shared by the tile map, the ECS, and A* pathfinding.
struct Vec2i {
    int x = 0;
    int y = 0;
};
inline bool operator==(Vec2i a, Vec2i b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Vec2i a, Vec2i b) { return !(a == b); }

// A point in framebuffer pixels (float so fractional grid positions are exact).
struct ScreenPt {
    float x = 0.0f;
    float y = 0.0f;
};

// Grid → screen. Accepts fractional (gx,gy) so a mid-tile agent maps smoothly.
inline ScreenPt grid_to_screen(float gx, float gy, float ox, float oy) {
    return ScreenPt{ ox + (gx - gy) * (kTileW * 0.5f),
                     oy + (gx + gy) * (kTileH * 0.5f) };
}

// Screen → grid, inverting the 2×2 system. Returns the *tile* the pixel falls in
// (floored). Derivation:
//     fx = (sx-ox)/(W/2) = gx - gy
//     fy = (sy-oy)/(H/2) = gx + gy   →   gx = (fx+fy)/2,  gy = (fy-fx)/2
inline Vec2i screen_to_grid(float sx, float sy, float ox, float oy) {
    const float fx = (sx - ox) / (kTileW * 0.5f);   // gx - gy
    const float fy = (sy - oy) / (kTileH * 0.5f);   // gx + gy
    const float gx = (fx + fy) * 0.5f;
    const float gy = (fy - fx) * 0.5f;
    return Vec2i{ static_cast<int>(std::floor(gx)),
                 static_cast<int>(std::floor(gy)) };
}

// Painter's-algorithm depth key. A bigger sum is NEARER the camera, so iso_render
// sorts ASCENDING and paints small-key (far) tiles first and big-key (near) tiles
// LAST, on top. Correct for 1×1 footprints (everything in M4); see iso_render.
inline float depth_key(float gx, float gy) { return gx + gy; }

} // namespace iso
