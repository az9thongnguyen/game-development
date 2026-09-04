// =============================================================================
//  engine/text/font.cpp  —  stb_truetype-backed font implementation
// =============================================================================
#include "engine/text/font.hpp"

#include <cmath>
#include <unordered_map>

#include "engine/text/utf8.hpp"

// The single translation unit that instantiates stb_truetype. Keeping the impl
// macro here (and nowhere else) means the header can be included freely elsewhere.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace text {
namespace {
constexpr char32_t kAsciiFirst = 32;    // ' '
constexpr char32_t kAsciiLast  = 126;   // '~'
} // namespace

// One cached glyph plus the coverage buffer it points into. Held by value in the
// per-size map: rehashing MOVES an Entry, and moving a std::vector transfers its
// heap buffer without reallocating, so `Glyph::cov` stays valid across growth.
struct Entry {
    Glyph                     g;
    std::vector<std::uint8_t> cov;
};

// A per-pixel-size cache: shared metrics plus one Entry per code point seen.
struct Size {
    float scale = 0.0f;
    int   ascent = 0, descent = 0, line_gap = 0;   // scaled to px
    int   px = 0;
    std::unordered_map<char32_t, Entry> glyphs;
};

struct Font::Impl {
    std::vector<std::uint8_t>                      ttf;    // owns the font bytes
    stbtt_fontinfo                                 info{};
    // ponytail: never evicted. Harmless for a fixed type scale (a handful of sizes);
    // a UI that lets the user drag a font-size slider would grow this without bound.
    std::unordered_map<int, std::unique_ptr<Size>> sizes;

    // Draw a hollow box for a code point the face has no outline for. Showing the
    // gap beats dropping the character: "missing glyph" is debuggable, silence is not.
    void make_tofu(Size& s, Entry& e) const {
        const int w = s.px * 5 / 10 > 2 ? s.px * 5 / 10 : 3;
        const int h = s.ascent * 3 / 4 > 2 ? s.ascent * 3 / 4 : 3;
        e.cov.assign(static_cast<std::size_t>(w) * h, 0);
        for (int x = 0; x < w; ++x) { e.cov[x] = 255; e.cov[static_cast<std::size_t>(h - 1) * w + x] = 255; }
        for (int y = 0; y < h; ++y) { e.cov[static_cast<std::size_t>(y) * w] = 255; e.cov[static_cast<std::size_t>(y) * w + (w - 1)] = 255; }
        e.g.w         = w;
        e.g.h         = h;
        e.g.advance   = w + (s.px / 8 > 1 ? s.px / 8 : 1);
        e.g.bearing_x = 0;
        e.g.top       = -h;              // sit the box on the baseline
        e.g.cov       = e.cov.data();
    }

    Entry& rasterize(Size& s, char32_t cp) {
        Entry& e = s.glyphs[cp];         // default-constructed on first sight

        if (stbtt_FindGlyphIndex(&info, static_cast<int>(cp)) == 0) { make_tofu(s, e); return e; }

        int aw, lsb;
        stbtt_GetCodepointHMetrics(&info, static_cast<int>(cp), &aw, &lsb);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&info, static_cast<int>(cp), s.scale, s.scale, &x0, &y0, &x1, &y1);
        const int w = x1 - x0, h = y1 - y0;

        e.g.advance   = static_cast<int>(std::lround(aw * s.scale));
        e.g.bearing_x = x0;
        e.g.top       = y0;              // top of glyph relative to baseline (y down)
        e.g.w         = w > 0 ? w : 0;
        e.g.h         = h > 0 ? h : 0;
        if (w > 0 && h > 0) {
            e.cov.resize(static_cast<std::size_t>(w) * h);
            stbtt_MakeCodepointBitmap(&info, e.cov.data(), w, h, w /*stride*/,
                                      s.scale, s.scale, static_cast<int>(cp));
            e.g.cov = e.cov.data();
        } else {
            e.g.cov = nullptr;           // blank glyph (space)
        }
        return e;
    }

    Size& size_for(int px) {
        if (px < 1) px = 1;
        auto it = sizes.find(px);
        if (it != sizes.end()) return *it->second;

        auto s     = std::make_unique<Size>();
        s->px      = px;
        s->scale   = stbtt_ScaleForPixelHeight(&info, static_cast<float>(px));
        int a, d, g;
        stbtt_GetFontVMetrics(&info, &a, &d, &g);
        s->ascent   = static_cast<int>(std::lround(a * s->scale));
        s->descent  = static_cast<int>(std::lround(d * s->scale));
        s->line_gap = static_cast<int>(std::lround(g * s->scale));

        // Printable ASCII up front — it is nearly every string, and doing it in one
        // pass keeps the first-use cost where it has always been. Everything else
        // (arrows, accents, CJK…) is rasterized the first time it is actually drawn.
        s->glyphs.reserve(128);
        for (char32_t cp = kAsciiFirst; cp <= kAsciiLast; ++cp) rasterize(*s, cp);

        Size& ref = *s;
        sizes.emplace(px, std::move(s));   // heap-stable: Entry (and its cov) move intact
        return ref;
    }
};

Font::Font() : p_(std::make_unique<Impl>()) {}
Font::~Font() = default;

std::unique_ptr<Font> Font::load_from_bytes(std::vector<std::uint8_t> ttf) {
    if (ttf.empty()) return nullptr;
    auto f     = std::unique_ptr<Font>(new Font());
    f->p_->ttf = std::move(ttf);
    const int off = stbtt_GetFontOffsetForIndex(f->p_->ttf.data(), 0);
    if (off < 0) return nullptr;
    if (!stbtt_InitFont(&f->p_->info, f->p_->ttf.data(), off)) return nullptr;
    return f;
}

int Font::ascent(int px)      { return p_->size_for(px).ascent; }
int Font::line_height(int px) {
    const Size& s = p_->size_for(px);
    return s.ascent - s.descent + s.line_gap;   // descent is negative
}

const Glyph* Font::glyph(int px, char32_t cp) {
    Size& s  = p_->size_for(px);
    auto  it = s.glyphs.find(cp);
    if (it != s.glyphs.end()) return &it->second.g;
    return &p_->rasterize(s, cp).g;   // first sight of this code point at this size
}

// Measured through the very same glyph() the draw loop uses, so a width and the
// pen positions that produce it cannot disagree.
int Font::text_width(int px, const char* s) {
    if (!s) return 0;
    const char* end = s;
    while (*end) ++end;
    int w = 0;
    for (const char* p = s; p < end; ) w += glyph(px, utf8_next(p, end))->advance;
    return w;
}

} // namespace text
