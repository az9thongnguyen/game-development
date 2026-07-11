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

#include <algorithm>  // std::swap
#include <cmath>      // std::floor, std::round
#include <cstdlib>    // std::abs

namespace gfx {
namespace {
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline std::uint8_t cov_of(float b) { return static_cast<std::uint8_t>(clamp01(b) * 255.0f + 0.5f); }
inline float fpart(float x)  { return x - std::floor(x); }
inline float rfpart(float x) { return 1.0f - fpart(x); }
} // namespace

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

// Vertical gradient (top→bottom), one lerp per PHYSICAL row so it's smooth even at
// small logical sizes. Used for scene backgrounds / raycaster sky & ground.
void Renderer2D::fill_v_gradient(int x, int y, int w, int h, Color top, Color bottom) {
    if (w <= 0 || h <= 0) return;
    const int px = x * ss_, py = y * ss_, pw = w * ss_, ph = h * ss_;
    const int r0 = r_of(top),    g0 = g_of(top),    b0 = b_of(top);
    const int r1 = r_of(bottom), g1 = g_of(bottom), b1 = b_of(bottom);
    for (int yy = 0; yy < ph; ++yy) {
        const float t = ph > 1 ? static_cast<float>(yy) / static_cast<float>(ph - 1) : 0.0f;
        const Color c = rgb(static_cast<std::uint8_t>(r0 + (r1 - r0) * t),
                            static_cast<std::uint8_t>(g0 + (g1 - g0) * t),
                            static_cast<std::uint8_t>(b0 + (b1 - b0) * t));
        fill_phys(px, py + yy, pw, 1, c);
    }
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

void Renderer2D::draw_line_aa(int lx0, int ly0, int lx1, int ly1, Color c) {
    // Xiaolin Wu in PHYSICAL space (endpoints scaled by ss_). Each step blends the
    // two pixels straddling the true line, weighted by distance → smooth edges.
    float x0 = lx0 * float(ss_), y0 = ly0 * float(ss_);
    float x1 = lx1 * float(ss_), y1 = ly1 * float(ss_);

    const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }   // fold to a shallow line
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); } // and left-to-right

    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float grad = (dx == 0.0f) ? 1.0f : dy / dx;

    // steep swapped x/y, so un-swap when writing: minor axis is `a`, major is `b`.
    auto plot = [&](int b, int a, float bright) {
        if (steep) blend_cov(a, b, c, cov_of(bright));     // (x,y) = (a,b)
        else       blend_cov(b, a, c, cov_of(bright));     // (x,y) = (b,a)
    };

    // Endpoint 1
    float xend  = std::round(x0);
    float yend  = y0 + grad * (xend - x0);
    float xgap  = rfpart(x0 + 0.5f);
    const int xpxl1 = int(xend);
    const int ypxl1 = int(std::floor(yend));
    plot(xpxl1, ypxl1,     rfpart(yend) * xgap);
    plot(xpxl1, ypxl1 + 1, fpart(yend)  * xgap);
    float intery = yend + grad;

    // Endpoint 2
    xend = std::round(x1);
    yend = y1 + grad * (xend - x1);
    xgap = fpart(x1 + 0.5f);
    const int xpxl2 = int(xend);
    const int ypxl2 = int(std::floor(yend));
    plot(xpxl2, ypxl2,     rfpart(yend) * xgap);
    plot(xpxl2, ypxl2 + 1, fpart(yend)  * xgap);

    // Main span
    for (int x = xpxl1 + 1; x < xpxl2; ++x) {
        plot(x, int(std::floor(intery)),     rfpart(intery));
        plot(x, int(std::floor(intery)) + 1, fpart(intery));
        intery += grad;
    }
}

// ---- anti-aliased shapes (analytic coverage) --------------------------------
namespace {
// Four corner boxes (top-left origin) + their quarter-disc centres, in physical px.
struct Corner { int bx, by, cx, cy; };
inline void corners_of(int px, int py, int pw, int ph, int pr, Corner out[4]) {
    out[0] = {px,           py,           px + pr,       py + pr};        // TL
    out[1] = {px + pw - pr, py,           px + pw - pr,  py + pr};        // TR
    out[2] = {px,           py + ph - pr, px + pr,       py + ph - pr};   // BL
    out[3] = {px + pw - pr, py + ph - pr, px + pw - pr,  py + ph - pr};   // BR
}
} // namespace

void Renderer2D::fill_round_rect(int x, int y, int w, int h, int radius, Color c) {
    if (w <= 0 || h <= 0) return;
    if (radius <= 0) { fill_rect(x, y, w, h, c); return; }
    const int px = x * ss_, py = y * ss_, pw = w * ss_, ph = h * ss_;
    int pr = radius * ss_;
    const int maxr = (pw < ph ? pw : ph) / 2;
    if (pr > maxr) pr = maxr;

    // Solid interior: the full-height middle band + the left/right straight bands.
    fill_phys(px + pr,      py,      pw - 2 * pr, ph,          c);
    fill_phys(px,           py + pr, pr,          ph - 2 * pr, c);
    fill_phys(px + pw - pr, py + pr, pr,          ph - 2 * pr, c);

    // Four AA corner quarter-discs.
    Corner cs[4]; corners_of(px, py, pw, ph, pr, cs);
    const float fr = float(pr);
    for (const Corner& k : cs)
        for (int Y = k.by; Y < k.by + pr; ++Y)
            for (int X = k.bx; X < k.bx + pr; ++X) {
                const float dx = X + 0.5f - k.cx, dy = Y + 0.5f - k.cy;
                blend_cov(X, Y, c, cov_of(fr + 0.5f - std::sqrt(dx * dx + dy * dy)));
            }
}

void Renderer2D::draw_round_rect(int x, int y, int w, int h, int radius, Color c) {
    if (w <= 0 || h <= 0) return;
    if (radius <= 0) { draw_rect(x, y, w, h, c); return; }
    const int px = x * ss_, py = y * ss_, pw = w * ss_, ph = h * ss_, t = ss_;
    int pr = radius * ss_;
    const int maxr = (pw < ph ? pw : ph) / 2;
    if (pr > maxr) pr = maxr;

    fill_phys(px + pr,      py,          pw - 2 * pr, t,           c);   // top
    fill_phys(px + pr,      py + ph - t, pw - 2 * pr, t,           c);   // bottom
    fill_phys(px,           py + pr,     t,           ph - 2 * pr, c);   // left
    fill_phys(px + pw - t,  py + pr,     t,           ph - 2 * pr, c);   // right

    Corner cs[4]; corners_of(px, py, pw, ph, pr, cs);
    const float fr = float(pr), ft = float(t);
    for (const Corner& k : cs)
        for (int Y = k.by; Y < k.by + pr; ++Y)
            for (int X = k.bx; X < k.bx + pr; ++X) {
                const float dx = X + 0.5f - k.cx, dy = Y + 0.5f - k.cy;
                const float d  = std::sqrt(dx * dx + dy * dy);
                const float ring = clamp01(fr + 0.5f - d) - clamp01(fr - ft + 0.5f - d);
                if (ring > 0.0f) blend_cov(X, Y, c, cov_of(ring));
            }
}

void Renderer2D::fill_circle(int lcx, int lcy, int lr, Color c) {
    if (lr <= 0) return;
    const float cx = lcx * float(ss_), cy = lcy * float(ss_), fr = lr * float(ss_);
    const int x0 = int(std::floor(cx - fr)), x1 = int(std::ceil(cx + fr));
    const int y0 = int(std::floor(cy - fr)), y1 = int(std::ceil(cy + fr));
    for (int Y = y0; Y <= y1; ++Y)
        for (int X = x0; X <= x1; ++X) {
            const float dx = X + 0.5f - cx, dy = Y + 0.5f - cy;
            blend_cov(X, Y, c, cov_of(fr + 0.5f - std::sqrt(dx * dx + dy * dy)));
        }
}

void Renderer2D::draw_circle(int lcx, int lcy, int lr, Color c) {
    if (lr <= 0) return;
    const float cx = lcx * float(ss_), cy = lcy * float(ss_), fr = lr * float(ss_), t = float(ss_);
    const int x0 = int(std::floor(cx - fr)), x1 = int(std::ceil(cx + fr));
    const int y0 = int(std::floor(cy - fr)), y1 = int(std::ceil(cy + fr));
    for (int Y = y0; Y <= y1; ++Y)
        for (int X = x0; X <= x1; ++X) {
            const float dx = X + 0.5f - cx, dy = Y + 0.5f - cy;
            const float d  = std::sqrt(dx * dx + dy * dy);
            const float ring = clamp01(fr + 0.5f - d) - clamp01(fr - t + 0.5f - d);
            if (ring > 0.0f) blend_cov(X, Y, c, cov_of(ring));
        }
}

void Renderer2D::drop_shadow(int x, int y, int w, int h, int radius,
                             int dx, int dy, int spread, Color c) {
    if (w <= 0 || h <= 0 || spread < 1) return;
    // Constant low per-layer alpha over `spread` concentric rings: a pixel near the
    // edge is inside few rings (faint), one near the panel inside many (darker) →
    // a natural radial falloff, and the AA rounded-rect softens each ring edge.
    std::uint8_t pa = a_of(c) / static_cast<std::uint32_t>(spread);
    if (pa == 0) pa = 1;
    const Color layer = (c & 0x00FFFFFFu) | (static_cast<std::uint32_t>(pa) << 24);
    for (int i = spread; i >= 1; --i)
        fill_round_rect(x + dx - i, y + dy - i, w + 2 * i, h + 2 * i, radius + i, layer);
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

void Renderer2D::blit_scaled(const Sprite& s, int dx, int dy, int dw, int dh) {
    if (!s.pixels || dw <= 0 || dh <= 0 || s.w <= 0 || s.h <= 0) return;
    for (int oy = 0; oy < dh; ++oy) {
        const int sy = oy * s.h / dh;                  // nearest source row
        for (int ox = 0; ox < dw; ++ox) {
            const int sx = ox * s.w / dw;              // nearest source col
            const Color src = s.pixels[sy * s.w + sx];
            if (a_of(src) == 0) continue;              // skip transparent
            const int bx = (dx + ox) * ss_, by = (dy + oy) * ss_;
            for (int py = 0; py < ss_; ++py)           // nearest ss×ss upscale
                for (int px = 0; px < ss_; ++px) blend_cov(bx + px, by + py, src, 255);
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
