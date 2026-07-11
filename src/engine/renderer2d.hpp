// =============================================================================
//  engine/renderer2d.hpp  —  hand-written 2D software renderer
// =============================================================================
//  Every primitive here writes directly into the platform framebuffer. Nothing
//  uses SDL drawing — this is the whole point. A Renderer2D is a thin, cheap
//  wrapper around one frame's Framebuffer; the App makes a fresh one each frame
//  and hands it to the scene via Context.
//
//  Coordinate system: (0,0) is the TOP-LEFT pixel, +x right, +y down.
// =============================================================================
#pragma once

#include <cstdint>

#include "engine/color.hpp"
#include "platform/platform.hpp"

namespace text { class Font; }   // fwd (full include lives in renderer2d.cpp)

namespace gfx {

// A source image to blit: tightly packed ARGB8888 pixels, row-major, w*h.
struct Sprite {
    const Color* pixels = nullptr;
    int          w = 0;
    int          h = 0;
};

class Renderer2D {
public:
    // `ss` is the supersample factor: the framebuffer `fb` is PHYSICALLY ss× the
    // logical size. The whole public API is in LOGICAL coordinates — every method
    // scales by ss internally — so game code is unaware of SSAA. ss=1 is a no-op.
    explicit Renderer2D(platform::Framebuffer fb, int ss = 1)
        : fb_(fb), ss_(ss < 1 ? 1 : ss) {}

    int width()  const { return fb_.width  / ss_; }   // LOGICAL size
    int height() const { return fb_.height / ss_; }
    int supersample() const { return ss_; }

    void clear(Color c);

    void set_pixel(int x, int y, Color c);    // opaque write, clipped (logical)
    void blend_pixel(int x, int y, Color c);  // alpha blend, clipped (logical)

    void fill_rect(int x, int y, int w, int h, Color c);
    void draw_rect(int x, int y, int w, int h, Color c);   // 1px outline
    void draw_line(int x0, int y0, int x1, int y1, Color c);          // aliased (Bresenham)
    void draw_line_aa(int x0, int y0, int x1, int y1, Color c);       // anti-aliased (Xiaolin Wu)

    // Anti-aliased shapes (analytic coverage). All args are LOGICAL; corners get
    // smooth quarter-disc edges. radius<=0 falls back to the crisp rect.
    void fill_round_rect(int x, int y, int w, int h, int radius, Color c);
    void draw_round_rect(int x, int y, int w, int h, int radius, Color c);  // ~1px AA outline
    void fill_circle(int cx, int cy, int r, Color c);
    void draw_circle(int cx, int cy, int r, Color c);                       // ~1px AA outline

    void blit(const Sprite& s, int x, int y);  // alpha-blended sprite

    // Text. With a font set (set_font), draw_text renders anti-aliased glyphs;
    // otherwise it falls back to the embedded 8x8 bitmap font at scale 1. `y` is
    // the top of the line in both cases.
    void set_font(text::Font* f, int px);
    void set_font_size(int px);                            // keep current font, change size
    int  text_width(const char* s) const;                  // px width of s in the current font (or 8x8)
    void draw_text(int x, int y, const char* s, Color c);  // font-backed (or 8x8 fallback)

    // Legacy 8x8 bitmap text (retro look / no-font paths). `scale` blocks each px.
    void draw_char(int x, int y, char ch, Color c, int scale = 1);
    void draw_text(int x, int y, const char* s, Color c, int scale);  // explicit 8x8

private:
    // Physical-space sinks (already scaled — NO logical→physical conversion). The
    // logical public methods above convert, then write through these; the AA
    // primitives rasterize in physical space and deposit partial coverage here.
    void fill_phys(int px, int py, int pw, int ph, Color c);           // solid rect
    void blend_cov(int px, int py, Color c, std::uint8_t coverage);    // coverage blend

    platform::Framebuffer fb_;
    text::Font*           font_    = nullptr;
    int                   font_px_ = 0;
    int                   ss_      = 1;    // supersample factor (physical = logical*ss_)
};

} // namespace gfx
