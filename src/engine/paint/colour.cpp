// =============================================================================
//  engine/paint/colour.cpp  —  see colour.hpp
// =============================================================================
#include "engine/paint/colour.hpp"

#include <algorithm>
#include <cmath>

namespace paint {

namespace {

float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Round to nearest rather than truncate. Truncating loses the round trip by one
// count on almost every colour — v*255 for a channel of 200 is 199.99998 in float,
// and an editor whose colour drifts a shade every time it is read is worse than one
// that cannot mix at all, because the drift is invisible until the file is compared.
std::uint8_t to_byte(float x) {
    const float v = clamp01(x) * 255.0f + 0.5f;
    return static_cast<std::uint8_t>(v > 255.0f ? 255.0f : v);
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

Hsv to_hsv(gfx::Color c) {
    const int r = gfx::r_of(c), g = gfx::g_of(c), b = gfx::b_of(c);
    const int mx = std::max({r, g, b});
    const int mn = std::min({r, g, b});
    const int d  = mx - mn;

    Hsv o;
    o.v = static_cast<float>(mx) / 255.0f;
    o.s = (mx == 0) ? 0.0f : static_cast<float>(d) / static_cast<float>(mx);
    // A grey has no hue to report. Returning 0 here is the lossy step the Hsv comment
    // is about, which is why a mixer keeps its own h across a drag instead of asking
    // this function again every frame.
    if (d == 0) return o;

    const float fd = static_cast<float>(d);
    float       h;
    if (mx == r)      h = 60.0f * static_cast<float>(g - b) / fd;
    else if (mx == g) h = 60.0f * (2.0f + static_cast<float>(b - r) / fd);
    else              h = 60.0f * (4.0f + static_cast<float>(r - g) / fd);
    if (h < 0.0f) h += 360.0f;
    o.h = h;
    return o;
}

gfx::Color from_hsv(Hsv c, std::uint8_t a) {
    float h = std::fmod(c.h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const float s = clamp01(c.s);
    const float v = clamp01(c.v);

    const float chroma = v * s;
    const float x      = chroma * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m      = v - chroma;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (static_cast<int>(h / 60.0f) % 6) {
        case 0: r = chroma; g = x;      break;
        case 1: r = x;      g = chroma; break;
        case 2: g = chroma; b = x;      break;
        case 3: g = x;      b = chroma; break;
        case 4: r = x;      b = chroma; break;
        default: r = chroma; b = x;     break;
    }
    return gfx::rgba(to_byte(r + m), to_byte(g + m), to_byte(b + m), a);
}

std::string to_hex(gfx::Color c) {
    static const char* d = "0123456789ABCDEF";
    std::string        s = "#";
    for (int shift = 28; shift >= 0; shift -= 4) s += d[(c >> shift) & 0xFu];
    return s;
}

std::optional<gfx::Color> parse_hex(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    if (a < b && s[a] == '#') ++a;
    s = s.substr(a, b - a);

    if (s.size() != 3 && s.size() != 6 && s.size() != 8) return std::nullopt;
    int v[8]{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        v[i] = hex_digit(s[i]);
        if (v[i] < 0) return std::nullopt;
    }

    // "#RGB" is the shorthand every artist types; each digit doubles (0xF -> 0xFF),
    // so #F00 is pure red and not a red 1/17th as bright.
    if (s.size() == 3)
        return gfx::rgba(static_cast<std::uint8_t>(v[0] * 17),
                         static_cast<std::uint8_t>(v[1] * 17),
                         static_cast<std::uint8_t>(v[2] * 17), 255);
    if (s.size() == 6)
        return gfx::rgba(static_cast<std::uint8_t>(v[0] * 16 + v[1]),
                         static_cast<std::uint8_t>(v[2] * 16 + v[3]),
                         static_cast<std::uint8_t>(v[4] * 16 + v[5]), 255);
    return gfx::rgba(static_cast<std::uint8_t>(v[2] * 16 + v[3]),
                     static_cast<std::uint8_t>(v[4] * 16 + v[5]),
                     static_cast<std::uint8_t>(v[6] * 16 + v[7]),
                     static_cast<std::uint8_t>(v[0] * 16 + v[1]));
}

} // namespace paint
