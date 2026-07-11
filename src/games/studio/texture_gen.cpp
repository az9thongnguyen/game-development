// =============================================================================
//  games/studio/texture_gen.cpp
// =============================================================================
#include "games/studio/texture_gen.hpp"

#include <algorithm>
#include <cmath>

namespace studio {
namespace {

uint8_t lerp8(uint8_t a, uint8_t b, double t) {
    return uint8_t(std::clamp(a + (double(b) - a) * t, 0.0, 255.0) + 0.5);
}
gfx::Color ramp(gfx::Color lo, gfx::Color hi, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return gfx::rgb(lerp8(gfx::r_of(lo), gfx::r_of(hi), t),
                    lerp8(gfx::g_of(lo), gfx::g_of(hi), t),
                    lerp8(gfx::b_of(lo), gfx::b_of(hi), t));
}
double apply_op(double t, const TextureParams& p) {
    switch (p.op) {
        case TextureParams::Op::Threshold: return t >= p.op_amount ? 1.0 : 0.0;
        case TextureParams::Op::Contrast: {
            const double k = 0.5 + p.op_amount * 3.0;               // slope 0.5..3.5
            return std::clamp((t - 0.5) * k + 0.5, 0.0, 1.0);
        }
        default: return t;
    }
}
double sample(const TextureParams& p, double u, double v) {
    switch (p.base) {
        case TextureParams::Base::Value:  return value_noise(u, v, p.frequency, p.seed);
        case TextureParams::Base::Perlin: return perlin_noise(u, v, p.frequency, p.seed);
        case TextureParams::Base::Checker: {
            const int cx = int(u * p.frequency), cy = int(v * p.frequency);
            return ((cx + cy) & 1) ? 1.0 : 0.0;                    // tiles if frequency is even
        }
        case TextureParams::Base::Wood: {
            const double n = fbm(u, v, p.basis, p.frequency, p.octaves, p.gain, p.lacunarity, p.seed);
            const double rings = std::sin((u + n * 0.5) * p.frequency * 6.28318530718);
            return rings * 0.5 + 0.5;
        }
        case TextureParams::Base::FBM:
        default: return fbm(u, v, p.basis, p.frequency, p.octaves, p.gain, p.lacunarity, p.seed);
    }
}

} // namespace

gfx::Image generate(const TextureParams& in) {
    TextureParams p = in;
    if (p.size < 8) p.size = 8;
    if (p.frequency < 1) p.frequency = 1;
    gfx::Image im;
    im.w = p.size; im.h = p.size;
    im.pixels.resize(static_cast<size_t>(p.size) * p.size);
    for (int y = 0; y < p.size; ++y) {
        const double v = double(y) / p.size;
        for (int x = 0; x < p.size; ++x) {
            const double u = double(x) / p.size;
            const double t = apply_op(sample(p, u, v), p);
            im.pixels[static_cast<size_t>(y) * p.size + x] = ramp(p.lo, p.hi, t);
        }
    }
    return im;
}

} // namespace studio
