// =============================================================================
//  games/studio/texture_gen.hpp  —  parametric, tileable texture generator
// =============================================================================
#pragma once
#include <cstdint>

#include "engine/image.hpp"
#include "games/studio/noise.hpp"

namespace studio {

struct TextureParams {
    std::uint32_t seed       = 1;
    int           size       = 128;               // square, clamped >=8
    enum class Base { FBM, Value, Perlin, Checker, Wood } base = Base::FBM;
    Basis         basis      = Basis::Perlin;     // basis used by FBM / Wood
    int           frequency  = 4;                 // lattice cells across a tile (>=1)
    int           octaves    = 4;                 // FBM
    double        gain       = 0.5;
    double        lacunarity = 2.0;               // keep integer for perfect tiling
    gfx::Color    lo         = gfx::rgb(30, 24, 18);
    gfx::Color    hi         = gfx::rgb(210, 180, 140);
    enum class Op { None, Threshold, Contrast } op = Op::None;
    double        op_amount  = 0.5;
};

// Deterministic + pure: same params -> identical pixels. Always seamlessly tileable.
gfx::Image generate(const TextureParams& p);

} // namespace studio
