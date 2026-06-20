// =============================================================================
//  engine/renderer2d.cpp  —  implementation of the 2D software renderer
// =============================================================================
#include "engine/renderer2d.hpp"

#include "engine/font8x8.hpp"

#include <cstdlib>  // std::abs

namespace gfx {

void Renderer2D::clear(Color c) {
    // Stride by pitch per row — pitch may exceed width for padded framebuffers.
    for (int y = 0; y < fb_.height; ++y) {
        Color* row = &fb_.pixels[y * fb_.pitch];
        for (int x = 0; x < fb_.width; ++x) row[x] = c;
    }
}

void Renderer2D::set_pixel(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= fb_.width || y >= fb_.height) return;  // clip
    fb_.pixels[y * fb_.pitch + x] = c;
}

void Renderer2D::blend_pixel(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= fb_.width || y >= fb_.height) return;
    Color& dst = fb_.pixels[y * fb_.pitch + x];
    dst = blend(dst, c);
}

void Renderer2D::fill_rect(int x, int y, int w, int h, Color c) {
    // Clip the rectangle to the framebuffer first, then fill solid row spans.
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > fb_.width)  x1 = fb_.width;
    int y1 = y + h; if (y1 > fb_.height) y1 = fb_.height;
    for (int yy = y0; yy < y1; ++yy) {
        Color* row = &fb_.pixels[yy * fb_.pitch];
        for (int xx = x0; xx < x1; ++xx) row[xx] = c;
    }
}

void Renderer2D::draw_rect(int x, int y, int w, int h, Color c) {
    if (w <= 0 || h <= 0) return;
    for (int xx = x; xx < x + w; ++xx) {     // top + bottom edges
        set_pixel(xx, y,         c);
        set_pixel(xx, y + h - 1, c);
    }
    for (int yy = y; yy < y + h; ++yy) {     // left + right edges
        set_pixel(x,         yy, c);
        set_pixel(x + w - 1, yy, c);
    }
}

void Renderer2D::draw_line(int x0, int y0, int x1, int y1, Color c) {
    // Bresenham's line algorithm: integer-only, works in all 8 octants. It walks
    // one pixel at a time, keeping an error term that decides when to step in the
    // minor axis — no floating point, no gaps.
    int dx =  std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        set_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Renderer2D::blit(const Sprite& s, int x, int y) {
    if (!s.pixels) return;
    for (int sy = 0; sy < s.h; ++sy) {
        const int py = y + sy;
        if (py < 0 || py >= fb_.height) continue;        // clip rows
        for (int sx = 0; sx < s.w; ++sx) {
            const int px = x + sx;
            if (px < 0 || px >= fb_.width) continue;      // clip columns
            const Color src = s.pixels[sy * s.w + sx];
            if (a_of(src) == 0) continue;                 // skip transparent
            Color& dst = fb_.pixels[py * fb_.pitch + px];
            dst = blend(dst, src);
        }
    }
}

void Renderer2D::draw_char(int x, int y, char ch, Color c, int scale) {
    unsigned uc = static_cast<unsigned char>(ch);
    if (uc >= 128) uc = static_cast<unsigned char>('?');
    const unsigned char* glyph = kFont8x8[uc];
    for (int row = 0; row < 8; ++row) {
        const unsigned bits = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (1u << col)) {                     // bit 0 == left-most
                if (scale == 1) {
                    set_pixel(x + col, y + row, c);
                } else {
                    fill_rect(x + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
    }
}

void Renderer2D::draw_text(int x, int y, const char* s, Color c, int scale) {
    int cx = x;
    for (; *s; ++s) {
        if (*s == '\n') { y += 8 * scale; cx = x; continue; }
        draw_char(cx, y, *s, c, scale);
        cx += 8 * scale;   // fixed-width font: 8px advance per glyph
    }
}

} // namespace gfx
