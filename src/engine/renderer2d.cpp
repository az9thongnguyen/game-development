// =============================================================================
//  engine/renderer2d.cpp  —  implementation of the 2D software renderer
// =============================================================================
//  Coordinate model (see the header): the PUBLIC API is in LOGICAL pixels; the
//  framebuffer `fb_` is PHYSICALLY `ss_`× larger. Each public primitive scales its
//  inputs by ss_ and writes through the two physical sinks (fill_phys / blend_cov).
//  ss_==1 makes every scale a no-op, so the renderer is byte-identical to before.
// =============================================================================
#include "engine/renderer2d.hpp"

#include "engine/font8x8.hpp"
#include "engine/text/font.hpp"

#include <cstdlib>  // std::abs

namespace gfx {

// ---- physical sinks (no scaling) --------------------------------------------
void Renderer2D::fill_phys(int x, int y, int w, int h, Color c) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > fb_.width)  x1 = fb_.width;
    int y1 = y + h; if (y1 > fb_.height) y1 = fb_.height;
    for (int yy = y0; yy < y1; ++yy) {
        Color* row = &fb_.pixels[yy * fb_.pitch];
        for (int xx = x0; xx < x1; ++xx) row[xx] = c;
    }
}

// Blend `c` at one physical pixel with an extra coverage multiplier (0..255)
// folded into its alpha. This is the shared anti-aliasing sink: font glyphs and
// the AA primitives (Wu lines, coverage shapes) all deposit partial coverage here.
void Renderer2D::blend_cov(int x, int y, Color c, std::uint8_t coverage) {
    if (coverage == 0) return;
    if (x < 0 || y < 0 || x >= fb_.width || y >= fb_.height) return;
    const std::uint32_t a = static_cast<std::uint32_t>(a_of(c)) * coverage / 255u;
    if (a == 0) return;
    Color& dst = fb_.pixels[y * fb_.pitch + x];
    dst = blend(dst, (c & 0x00FFFFFFu) | (a << 24));
}

// ---- logical primitives (scale by ss_, then hit the sinks) ------------------
void Renderer2D::clear(Color c) {
    for (int y = 0; y < fb_.height; ++y) {
        Color* row = &fb_.pixels[y * fb_.pitch];
        for (int x = 0; x < fb_.width; ++x) row[x] = c;
    }
}

void Renderer2D::set_pixel(int x, int y, Color c) {
    fill_phys(x * ss_, y * ss_, ss_, ss_, c);          // one logical px = ss×ss block, opaque
}

void Renderer2D::blend_pixel(int x, int y, Color c) {
    const int bx = x * ss_, by = y * ss_;
    for (int dy = 0; dy < ss_; ++dy)
        for (int dx = 0; dx < ss_; ++dx) blend_cov(bx + dx, by + dy, c, 255);
}

void Renderer2D::fill_rect(int x, int y, int w, int h, Color c) {
    fill_phys(x * ss_, y * ss_, w * ss_, h * ss_, c);
}

void Renderer2D::draw_rect(int x, int y, int w, int h, Color c) {
    if (w <= 0 || h <= 0) return;
    const int t = ss_;                                 // 1 logical px = ss physical px thick
    fill_phys(x * ss_,            y * ss_,           w * ss_, t,       c);   // top
    fill_phys(x * ss_,            (y + h - 1) * ss_, w * ss_, t,       c);   // bottom
    fill_phys(x * ss_,            y * ss_,           t,       h * ss_, c);   // left
    fill_phys((x + w - 1) * ss_,  y * ss_,           t,       h * ss_, c);   // right
}

void Renderer2D::draw_line(int x0, int y0, int x1, int y1, Color c) {
    // Bresenham in LOGICAL space; set_pixel scales each step to an ss×ss block.
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
        for (int sx = 0; sx < s.w; ++sx) {
            const Color src = s.pixels[sy * s.w + sx];
            if (a_of(src) == 0) continue;              // skip transparent
            const int bx = (x + sx) * ss_, by = (y + sy) * ss_;
            for (int dy = 0; dy < ss_; ++dy)           // nearest ss×ss upscale
                for (int dx = 0; dx < ss_; ++dx) blend_cov(bx + dx, by + dy, src, 255);
        }
    }
}

// ---- 8x8 bitmap text (legacy / fallback) ------------------------------------
void Renderer2D::draw_char(int x, int y, char ch, Color c, int scale) {
    unsigned uc = static_cast<unsigned char>(ch);
    if (uc >= 128) uc = static_cast<unsigned char>('?');
    const unsigned char* glyph = kFont8x8[uc];
    for (int row = 0; row < 8; ++row) {
        const unsigned bits = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (1u << col)) {                  // bit 0 == left-most
                if (scale == 1) set_pixel(x + col, y + row, c);
                else            fill_rect(x + col * scale, y + row * scale, scale, scale, c);
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

// ---- font-backed (anti-aliased) text ----------------------------------------
void Renderer2D::set_font(text::Font* f, int px) { font_ = f; font_px_ = px; }
void Renderer2D::set_font_size(int px)           { font_px_ = px; }

int Renderer2D::text_width(const char* s) const {
    if (font_ && font_px_ > 0) return font_->text_width(font_px_ * ss_, s) / ss_;
    int n = 0; for (const char* p = s; p && *p; ++p) ++n;   // 8x8 fallback: 8px/char
    return n * 8;
}

void Renderer2D::draw_text(int x, int y, const char* s, Color c) {
    if (!s) return;
    if (!font_ || font_px_ <= 0) { draw_text(x, y, s, c, 1); return; }  // 8x8 fallback

    const int rpx  = font_px_ * ss_;               // rasterize at PHYSICAL size → crisp
    const int lh   = font_->line_height(rpx);
    const int asc  = font_->ascent(rpx);
    int       base = y * ss_ + asc;                 // physical baseline (y is the line top)
    int       pen  = x * ss_;
    for (; *s; ++s) {
        if (*s == '\n') { base += lh; pen = x * ss_; continue; }
        const text::Glyph* g = font_->glyph(rpx, *s);
        if (!g) continue;
        if (g->cov) {
            const int gx = pen + g->bearing_x;
            const int gy = base + g->top;
            for (int ry = 0; ry < g->h; ++ry) {
                const std::uint8_t* row = g->cov + static_cast<std::size_t>(ry) * g->w;
                for (int rx = 0; rx < g->w; ++rx) blend_cov(gx + rx, gy + ry, c, row[rx]);
            }
        }
        pen += g->advance;
    }
}

} // namespace gfx
