// =============================================================================
//  tests/test_studio.cpp  —  mini-studio core tests (dependency-free, CTest)
// =============================================================================
//  Covers the PURE studio core: seamless noise (value/Perlin/fBm), the parametric
//  texture generator, the .hrt encode/decode round-trip, and the re-editable recipe.
//  No SDL, no window — image.cpp + assets.cpp are compiled straight into this binary.
// =============================================================================
#include "games/studio/noise.hpp"
#include "games/studio/texture_gen.hpp"
#include "games/studio/recipe.hpp"
#include "engine/image.hpp"

#include <cmath>
#include <cstdio>

using namespace studio;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool approx(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps * (1.0 + std::fabs(a) + std::fabs(b));
}

// ---- noise (Task 1) --------------------------------------------------------

static void test_noise_range() {
    for (int i = 0; i < 50; ++i) {
        const double u = i * 0.017, v = i * 0.031;
        const double a = value_noise(u, v, 4, 7);
        const double b = perlin_noise(u, v, 4, 7);
        const double f = fbm(u, v, Basis::Perlin, 4, 5, 0.5, 2.0, 7);
        CHECK(a >= 0.0 && a <= 1.0);
        CHECK(b >= -1e-9 && b <= 1.0 + 1e-9);
        CHECK(f >= 0.0 && f <= 1.0);
    }
}

static void test_noise_deterministic() {
    CHECK(approx(value_noise(0.3, 0.4, 4, 7), value_noise(0.3, 0.4, 4, 7)));
    CHECK(!approx(value_noise(0.3, 0.4, 4, 7), value_noise(0.3, 0.4, 4, 8)));  // seed matters
}

static void test_noise_periodic() {
    // period 1.0 in u and v -> seamless tiling. Must hold for all three bases.
    CHECK(approx(value_noise(0.3, 0.4, 4, 7),  value_noise(1.3, 0.4, 4, 7)));
    CHECK(approx(value_noise(0.3, 0.4, 4, 7),  value_noise(0.3, 1.4, 4, 7)));
    CHECK(approx(perlin_noise(0.3, 0.4, 4, 7), perlin_noise(1.3, 0.4, 4, 7)));
    CHECK(approx(fbm(0.3, 0.4, Basis::Perlin, 4, 4, 0.5, 2.0, 7),
                 fbm(1.3, 0.4, Basis::Perlin, 4, 4, 0.5, 2.0, 7)));
    CHECK(approx(fbm(0.3, 0.4, Basis::Value, 3, 3, 0.5, 2.0, 9),
                 fbm(0.3, 1.4, Basis::Value, 3, 3, 0.5, 2.0, 9)));
}

// ---- texture generator + encode_hrt (Task 2) -------------------------------

static void test_texture_deterministic() {
    TextureParams p;                       // defaults: FBM/Perlin, 128, freq 4
    const gfx::Image a = generate(p);
    const gfx::Image b = generate(p);
    CHECK(a.w == 128 && a.h == 128);
    CHECK(a.pixels == b.pixels);           // byte-identical
    TextureParams q = p; q.seed = 2;
    CHECK(!(generate(q).pixels == a.pixels));   // seed changes output
}

static void test_texture_size_clamp() {
    TextureParams p; p.size = 2; p.frequency = 0;
    const gfx::Image im = generate(p);
    CHECK(im.w == 8 && im.h == 8);          // size clamped to 8, freq clamped to 1
}

static void test_hrt_roundtrip() {
    TextureParams p; p.size = 32;
    const gfx::Image im = generate(p);
    const std::vector<uint8_t> bytes = gfx::encode_hrt(im);
    CHECK(bytes.size() == size_t(12 + 32 * 32 * 4));
    const auto back = gfx::decode_hrt(bytes);
    CHECK(back.has_value());
    CHECK(back->w == im.w && back->h == im.h);
    CHECK(back->pixels == im.pixels);       // exact round-trip
}

// ---- recipe (Task 3) -------------------------------------------------------

static void test_recipe_roundtrip() {
    TextureParams p;
    p.seed = 42; p.size = 64; p.base = TextureParams::Base::Wood;
    p.basis = Basis::Value; p.frequency = 6; p.octaves = 3;
    p.gain = 0.6; p.lacunarity = 2.0; p.lo = gfx::rgb(10, 20, 30);
    p.hi = gfx::rgb(200, 190, 180); p.op = TextureParams::Op::Contrast; p.op_amount = 0.7;
    // Re-serializing the parsed recipe must reproduce the original text exactly.
    const std::string once = to_recipe(p);
    CHECK(to_recipe(from_recipe(once)) == once);
    // And the parsed params must regenerate identical pixels.
    CHECK(generate(from_recipe(once)).pixels == generate(p).pixels);
}

int main() {
    test_noise_range();
    test_noise_deterministic();
    test_noise_periodic();
    test_texture_deterministic();
    test_texture_size_clamp();
    test_hrt_roundtrip();
    test_recipe_roundtrip();
    if (g_failures == 0) std::printf("studio: all tests passed\n");
    else                 std::printf("studio: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
