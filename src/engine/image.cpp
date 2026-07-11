// =============================================================================
//  engine/image.cpp
// =============================================================================
#include "engine/image.hpp"

#include <cstdint>

#include "engine/assets.hpp"

namespace gfx {
namespace {
uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}
} // namespace

std::optional<Image> decode_hrt(const std::vector<uint8_t>& b) {
    if (b.size() < 12) return std::nullopt;
    if (!(b[0] == 'H' && b[1] == 'R' && b[2] == 'T' && b[3] == '1')) return std::nullopt;

    const uint32_t w = read_be32(&b[4]);
    const uint32_t h = read_be32(&b[8]);
    // Bound the dimensions BEFORE computing the byte count: on a 32-bit target
    // (wasm32) `size_t(w)*h*4` could overflow and wrap the size check, and a huge w/h
    // would also cast to a negative int. 16384 is a generous texture cap.
    if (w == 0 || h == 0 || w > 16384 || h > 16384) return std::nullopt;
    const size_t   need = 12 + static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    if (b.size() < need) return std::nullopt;

    Image img;
    img.w = static_cast<int>(w);
    img.h = static_cast<int>(h);
    img.pixels.resize(static_cast<size_t>(w) * h);

    const uint8_t* px = &b[12];
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const uint8_t r = px[i * 4 + 0];
        const uint8_t g = px[i * 4 + 1];
        const uint8_t bl = px[i * 4 + 2];
        const uint8_t a = px[i * 4 + 3];
        img.pixels[i] = rgba(r, g, bl, a);  // RGBA bytes -> ARGB8888 Color
    }
    return img;
}

std::vector<uint8_t> encode_hrt(const Image& img) {
    std::vector<uint8_t> out;
    if (img.w <= 0 || img.h <= 0) return out;
    out.reserve(12 + static_cast<size_t>(img.w) * img.h * 4);
    const char magic[4] = {'H', 'R', 'T', '1'};
    out.insert(out.end(), magic, magic + 4);
    auto be32 = [&](uint32_t v) {
        out.push_back(uint8_t(v >> 24)); out.push_back(uint8_t(v >> 16));
        out.push_back(uint8_t(v >> 8));  out.push_back(uint8_t(v));
    };
    be32(static_cast<uint32_t>(img.w));
    be32(static_cast<uint32_t>(img.h));
    for (Color c : img.pixels) {
        out.push_back(r_of(c)); out.push_back(g_of(c));
        out.push_back(b_of(c)); out.push_back(a_of(c));
    }
    return out;
}

std::optional<Image> load_image(const std::string& path) {
    auto bytes = assets::load_file(path);   // I/O via the seam (web-portable)
    if (!bytes) return std::nullopt;
    return decode_hrt(*bytes);              // pure bytes->Image (reused by the asset cache)
}

} // namespace gfx
