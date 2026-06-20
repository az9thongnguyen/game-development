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

#include "engine/color.hpp"
#include "platform/platform.hpp"

namespace gfx {

// A source image to blit: tightly packed ARGB8888 pixels, row-major, w*h.
struct Sprite {
    const Color* pixels = nullptr;
    int          w = 0;
    int          h = 0;
};

class Renderer2D {
public:
    explicit Renderer2D(platform::Framebuffer fb) : fb_(fb) {}

    int width()  const { return fb_.width; }
    int height() const { return fb_.height; }

    void clear(Color c);

    void set_pixel(int x, int y, Color c);    // opaque write, clipped
    void blend_pixel(int x, int y, Color c);  // alpha blend, clipped

    void fill_rect(int x, int y, int w, int h, Color c);
    void draw_rect(int x, int y, int w, int h, Color c);   // 1px outline
    void draw_line(int x0, int y0, int x1, int y1, Color c);

    void blit(const Sprite& s, int x, int y);  // alpha-blended sprite

    // 8x8 bitmap text. `scale` draws each font pixel as a scale*scale block.
    void draw_char(int x, int y, char ch, Color c, int scale = 1);
    void draw_text(int x, int y, const char* s, Color c, int scale = 1);

private:
    platform::Framebuffer fb_;
};

} // namespace gfx
