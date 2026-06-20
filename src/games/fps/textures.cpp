// =============================================================================
//  games/fps/textures.cpp
// =============================================================================
#include "games/fps/textures.hpp"

#include <cmath>

#include "engine/color.hpp"

namespace fps {
namespace {

constexpr int T = 64;  // texture size (power of two)

int cl(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// Cheap deterministic hash for per-texel variation (no RNG needed).
uint32_t hash2(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x * 73856093) ^ static_cast<uint32_t>(y * 19349663);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

gfx::Image blank() {
    gfx::Image im; im.w = T; im.h = T;
    im.pixels.assign(static_cast<size_t>(T) * T, 0xFF000000u);
    return im;
}

gfx::Image make_brick() {  // reddish brick with offset rows + mortar
    gfx::Image im = blank();
    for (int y = 0; y < T; ++y)
        for (int x = 0; x < T; ++x) {
            const int row = y / 16, off = (row % 2) * 8;
            const bool mortar = (y % 16 < 2) || (((x + off) % 16) < 2);
            const int n = static_cast<int>(hash2((x + off) / 16, row) % 26) - 13;
            im.pixels[static_cast<size_t>(y) * T + x] =
                mortar ? gfx::rgb(74, 68, 60)
                       : gfx::rgb(cl(150 + n), cl(62 + n / 2), cl(48 + n / 2));
        }
    return im;
}

gfx::Image make_stone() {  // grey blocks with seams
    gfx::Image im = blank();
    for (int y = 0; y < T; ++y)
        for (int x = 0; x < T; ++x) {
            const int n = static_cast<int>(hash2(x, y) % 44) - 22;
            int v = cl(142 + n);
            if ((y % 32) < 1 || (x % 32) < 1) v = v * 3 / 4;  // mortar seams
            im.pixels[static_cast<size_t>(y) * T + x] = gfx::rgb(cl(v), cl(v), cl(v + 8));
        }
    return im;
}

gfx::Image make_wood() {  // greenish panelling for pillars
    gfx::Image im = blank();
    for (int y = 0; y < T; ++y)
        for (int x = 0; x < T; ++x) {
            const int grain = static_cast<int>(hash2(x / 2, y / 10) % 24) - 12;
            const int band  = ((y % 16) < 2) ? -22 : 0;
            im.pixels[static_cast<size_t>(y) * T + x] =
                gfx::rgb(cl(110 + grain + band), cl(150 + grain + band), cl(92 + grain + band));
        }
    return im;
}

} // namespace

WallTextures make_wall_textures() {
    WallTextures w;
    w.tex[1] = make_stone();  // border
    w.tex[2] = make_brick();  // room walls
    w.tex[3] = make_wood();   // pillars
    return w;
}

gfx::Image make_barrel() {
    gfx::Image im; im.w = T; im.h = T;
    im.pixels.assign(static_cast<size_t>(T) * T, 0u);  // fully transparent
    const int cx = T / 2;
    for (int y = 6; y < T - 3; ++y) {
        const double yn = (y - 6) / static_cast<double>(T - 10);  // 0..1 down the body
        if (yn < 0.0 || yn > 1.0) continue;
        const double bulge = std::sin(yn * 3.14159265);            // fat in the middle
        const int halfw = static_cast<int>((T * 0.30) * (0.72 + 0.28 * bulge));
        const bool hoop = (y % 14) < 2;                            // metal bands
        for (int x = cx - halfw; x <= cx + halfw; ++x) {
            const int n = static_cast<int>(hash2(x, y) % 20) - 10;
            im.pixels[static_cast<size_t>(y) * T + x] =
                hoop ? gfx::rgb(60, 55, 50)
                     : gfx::rgb(cl(124 + n), cl(74 + n), cl(40 + n));  // wood brown
        }
    }
    return im;
}

} // namespace fps
