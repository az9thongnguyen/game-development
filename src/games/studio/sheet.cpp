// =============================================================================
//  games/studio/sheet.cpp
// =============================================================================
#include "games/studio/sheet.hpp"

namespace studio {

gfx::Image make_sheet(const TextureParams& p, int frames) {
    gfx::Image base = generate(p);
    if (frames <= 1 || base.w <= 0) return base;

    const int size = base.w;                     // generate() is square
    gfx::Image sheet;
    sheet.w = size;
    sheet.h = size * frames;
    sheet.pixels.resize(std::size_t(sheet.w) * sheet.h);

    for (int f = 0; f < frames; ++f) {
        const int shift = (f * size) / frames;   // horizontal scroll for this frame
        for (int y = 0; y < size; ++y) {
            const gfx::Color* srow = &base.pixels[std::size_t(y) * size];
            gfx::Color*       drow = &sheet.pixels[(std::size_t(f) * size + y) * size];
            for (int x = 0; x < size; ++x)
                drow[x] = srow[(x + shift) % size];   // wraps seamlessly (tileable)
        }
    }
    return sheet;
}

} // namespace studio
