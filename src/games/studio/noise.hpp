// =============================================================================
//  games/studio/noise.hpp  —  seamless procedural noise (hand-written)
// =============================================================================
//  All bases are PERIODIC with period 1.0 in u and v: noise(u,v)==noise(u+1,v)
//  ==noise(u,v+1). That periodicity is exactly what makes a texture sampled over
//  [0,1)^2 tile seamlessly — the seam between adjacent tiles is continuous.
// =============================================================================
#pragma once
#include <cstdint>

namespace studio {

// Value noise at (u,v), lattice wrapped on a `period`x`period` torus. Result in [0,1).
double value_noise(double u, double v, int period, std::uint32_t seed);

// Gradient (Perlin) noise, same torus wrap. Result remapped from [-1,1] to [0,1].
double perlin_noise(double u, double v, int period, std::uint32_t seed);

enum class Basis { Value, Perlin };

// Fractal Brownian motion over [0,1)^2: `octaves` layers, frequency *= lacunarity
// and amplitude *= gain each octave, normalized to [0,1]. base_freq (>=1) is the
// lattice size of octave 0; INTEGER lacunarity keeps every octave tileable.
double fbm(double u, double v, Basis basis, int base_freq, int octaves,
           double gain, double lacunarity, std::uint32_t seed);

} // namespace studio
