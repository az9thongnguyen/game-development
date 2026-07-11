// =============================================================================
//  games/studio/noise.cpp
// =============================================================================
#include "games/studio/noise.hpp"

#include <cmath>

namespace studio {
namespace {

// 2D integer hash -> uint32 (same family as fps::hash2, plus a seed term).
std::uint32_t hash2(int x, int y, std::uint32_t seed) {
    std::uint32_t h = std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u
                    ^ (seed * 83492791u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
int    wrap(int a, int p)                       { int m = a % p; return m < 0 ? m + p : m; }
double fade(double t)                           { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
double lerp(double a, double b, double t)       { return a + (b - a) * t; }
double hash01(int x, int y, std::uint32_t seed) { return hash2(x, y, seed) / 4294967296.0; }

void grad(int x, int y, std::uint32_t seed, double& gx, double& gy) {
    static const double d[8][2] = {
        { 1, 0}, {-1, 0}, {0, 1}, {0,-1},
        { 0.70710678, 0.70710678}, {-0.70710678, 0.70710678},
        { 0.70710678,-0.70710678}, {-0.70710678,-0.70710678}};
    const int i = int(hash2(x, y, seed) & 7u);
    gx = d[i][0]; gy = d[i][1];
}

} // namespace

double value_noise(double u, double v, int period, std::uint32_t seed) {
    if (period < 1) period = 1;
    const double x = u * period, y = v * period;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    const int x0 = wrap(ix, period), x1 = wrap(ix + 1, period);
    const int y0 = wrap(iy, period), y1 = wrap(iy + 1, period);
    const double v00 = hash01(x0, y0, seed), v10 = hash01(x1, y0, seed);
    const double v01 = hash01(x0, y1, seed), v11 = hash01(x1, y1, seed);
    const double su = fade(fx), sv = fade(fy);
    return lerp(lerp(v00, v10, su), lerp(v01, v11, su), sv);
}

double perlin_noise(double u, double v, int period, std::uint32_t seed) {
    if (period < 1) period = 1;
    const double x = u * period, y = v * period;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    const int x0 = wrap(ix, period), x1 = wrap(ix + 1, period);
    const int y0 = wrap(iy, period), y1 = wrap(iy + 1, period);
    auto dot = [&](int gxi, int gyi, double dx, double dy) {
        double gx, gy; grad(gxi, gyi, seed, gx, gy); return gx * dx + gy * dy;
    };
    const double n00 = dot(x0, y0, fx,       fy);
    const double n10 = dot(x1, y0, fx - 1.0, fy);
    const double n01 = dot(x0, y1, fx,       fy - 1.0);
    const double n11 = dot(x1, y1, fx - 1.0, fy - 1.0);
    const double su = fade(fx), sv = fade(fy);
    const double n = lerp(lerp(n00, n10, su), lerp(n01, n11, su), sv);  // ~[-1,1]
    return n * 0.5 + 0.5;
}

double fbm(double u, double v, Basis basis, int base_freq, int octaves,
           double gain, double lacunarity, std::uint32_t seed) {
    if (base_freq < 1) base_freq = 1;
    if (octaves   < 1) octaves   = 1;
    double sum = 0.0, amp = 1.0, norm = 0.0;
    int freq = base_freq;
    for (int o = 0; o < octaves; ++o) {
        const std::uint32_t s = seed + std::uint32_t(o) * 101u;
        const double n = (basis == Basis::Perlin) ? perlin_noise(u, v, freq, s)
                                                  : value_noise(u, v, freq, s);
        sum  += amp * n;
        norm += amp;
        amp  *= gain;
        freq  = int(std::lround(freq * lacunarity));
        if (freq < 1) freq = 1;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

} // namespace studio
