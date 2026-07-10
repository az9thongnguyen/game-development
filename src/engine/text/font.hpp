// =============================================================================
//  engine/text/font.hpp  —  anti-aliased font rendering (stb_truetype)
// =============================================================================
//  Replaces the 8x8 bitmap font with real, scalable, anti-aliased glyphs. A Font
//  wraps one .ttf face; for each pixel size you ask for, it lazily rasterizes an
//  ASCII glyph atlas (32..126) into 8-bit coverage bitmaps and caches it.
//
//  Layering: this module is PURE — it knows nothing about files or SDL. The
//  caller loads the .ttf bytes (through the `assets::` seam, so the web VFS works)
//  and hands them to `Font::load_from_bytes`. stb_truetype is confined to font.cpp
//  (pimpl), so consumers never see the third-party header or its impl macro.
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace text {

// One rasterized glyph. `cov` points into the owning Font's per-size atlas (stable
// for the Font's lifetime); it is `w*h` 8-bit coverage, row-major, 0=transparent
// 255=solid. `cov` is null for blank glyphs (e.g. space) — check before reading.
struct Glyph {
    int w = 0, h = 0;                     // coverage bitmap size (px)
    int advance = 0;                      // pen advance to the next glyph (px)
    int bearing_x = 0;                    // left offset from the pen (px)
    int top = 0;                          // top row offset from the baseline (px, usually negative = above)
    const std::uint8_t* cov = nullptr;    // w*h coverage, owned by the Font
};

class Font {
public:
    // Build a Font from raw .ttf bytes. Returns nullptr if the data can't be
    // parsed. The bytes are copied and kept alive internally.
    static std::unique_ptr<Font> load_from_bytes(std::vector<std::uint8_t> ttf);

    ~Font();
    Font(const Font&)            = delete;
    Font& operator=(const Font&) = delete;

    int ascent(int px);                          // px from the line top to the baseline
    int line_height(int px);                     // recommended line-to-line advance
    int text_width(int px, const char* s);       // sum of advances (no kerning)
    const Glyph* glyph(int px, char c);          // builds the px atlas on first use

private:
    Font();
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace text
