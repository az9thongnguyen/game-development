// =============================================================================
//  engine/text/font.cpp  —  stb_truetype-backed font implementation
// =============================================================================
#include "engine/text/font.hpp"

#include <cmath>
#include <unordered_map>

// The single translation unit that instantiates stb_truetype. Keeping the impl
// macro here (and nowhere else) means the header can be included freely elsewhere.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace text {
namespace {
constexpr int kFirst = 32;    // ' '
constexpr int kLast  = 126;   // '~'
constexpr int kCount = kLast - kFirst + 1;   // 95 printable ASCII
inline int index_of(char c) {
    unsigned uc = static_cast<unsigned char>(c);
    if (uc < kFirst || uc > kLast) uc = static_cast<unsigned char>('?');
    return static_cast<int>(uc) - kFirst;
}
} // namespace

// A per-pixel-size atlas: metrics plus one coverage buffer per printable glyph.
struct Size {
    float scale = 0.0f;
    int   ascent = 0, descent = 0, line_gap = 0;   // scaled to px
    std::vector<std::uint8_t> cov[kCount];
    Glyph glyph[kCount];
};

struct Font::Impl {
    std::vector<std::uint8_t>                       ttf;    // owns the font bytes
    stbtt_fontinfo                                  info{};
    std::unordered_map<int, std::unique_ptr<Size>> sizes;

    Size& size_for(int px) {
        if (px < 1) px = 1;
        auto it = sizes.find(px);
        if (it != sizes.end()) return *it->second;

        auto s     = std::make_unique<Size>();
        s->scale   = stbtt_ScaleForPixelHeight(&info, static_cast<float>(px));
        int a, d, g;
        stbtt_GetFontVMetrics(&info, &a, &d, &g);
        s->ascent   = static_cast<int>(std::lround(a * s->scale));
        s->descent  = static_cast<int>(std::lround(d * s->scale));
        s->line_gap = static_cast<int>(std::lround(g * s->scale));

        for (int i = 0; i < kCount; ++i) {
            const int cp = kFirst + i;
            int aw, lsb;
            stbtt_GetCodepointHMetrics(&info, cp, &aw, &lsb);
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&info, cp, s->scale, s->scale, &x0, &y0, &x1, &y1);
            const int w = x1 - x0, h = y1 - y0;

            Glyph& gl    = s->glyph[i];
            gl.advance   = static_cast<int>(std::lround(aw * s->scale));
            gl.bearing_x = x0;
            gl.top       = y0;              // top of glyph relative to baseline (y down)
            gl.w         = w > 0 ? w : 0;
            gl.h         = h > 0 ? h : 0;
            if (w > 0 && h > 0) {
                s->cov[i].resize(static_cast<std::size_t>(w) * h);
                stbtt_MakeCodepointBitmap(&info, s->cov[i].data(), w, h, w /*stride*/,
                                          s->scale, s->scale, cp);
                gl.cov = s->cov[i].data();
            } else {
                gl.cov = nullptr;          // blank glyph (space)
            }
        }

        Size& ref = *s;
        sizes.emplace(px, std::move(s));   // heap-stable: glyph.cov pointers stay valid
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

int Font::text_width(int px, const char* s) {
    if (!s) return 0;
    const Size& sz = p_->size_for(px);
    int w = 0;
    for (; *s; ++s) w += sz.glyph[index_of(*s)].advance;
    return w;
}

const Glyph* Font::glyph(int px, char c) {
    return &p_->size_for(px).glyph[index_of(c)];
}

} // namespace text
