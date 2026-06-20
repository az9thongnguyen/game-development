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

std::optional<Image> load_image(const std::string& path) {
    auto bytes = assets::load_file(path);
    if (!bytes) return std::nullopt;
    const std::vector<uint8_t>& b = *bytes;

    if (b.size() < 12) return std::nullopt;
    if (!(b[0] == 'H' && b[1] == 'R' && b[2] == 'T' && b[3] == '1')) return std::nullopt;

    const uint32_t w = read_be32(&b[4]);
    const uint32_t h = read_be32(&b[8]);
    const size_t   need = 12 + static_cast<size_t>(w) * h * 4;
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

} // namespace gfx
