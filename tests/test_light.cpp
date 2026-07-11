// =============================================================================
//  tests/test_light.cpp  —  2D radial light math (dependency-free, CTest)
// =============================================================================
//  light_falloff pins its ends and is monotone; light_sample turns falloff into
//  an additive-weight alpha while preserving the light's RGB. No SDL, no IO.
// =============================================================================
#include "engine/fx/light.hpp"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_failures; } \
    } while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

int main() {
    // Falloff: full at centre, zero at/beyond radius, guarded for radius<=0.
    CHECK(near(fx::light_falloff(0.0f, 100.0f), 1.0f));
    CHECK(near(fx::light_falloff(100.0f, 100.0f), 0.0f));
    CHECK(near(fx::light_falloff(150.0f, 100.0f), 0.0f));    // clamped past the edge
    CHECK(fx::light_falloff(50.0f, 0.0f) == 0.0f);           // radius guard

    // Monotonically decreasing from centre to edge.
    float prev = 2.0f;
    for (int i = 0; i <= 10; ++i) {
        const float f = fx::light_falloff(float(i) * 10.0f, 100.0f);
        CHECK(f <= prev + 1e-6f);
        prev = f;
    }
    // Interior value strictly between the ends.
    CHECK(fx::light_falloff(50.0f, 100.0f) > 0.0f && fx::light_falloff(50.0f, 100.0f) < 1.0f);

    // Sample at the centre: max alpha, RGB preserved.
    {
        fx::Light L; L.x = 200; L.y = 100; L.radius = 80;
        L.color = gfx::rgb(255, 240, 200); L.intensity = 1.0f;
        const gfx::Color c = fx::light_sample(L, 200, 100);
        CHECK(gfx::a_of(c) == 255);
        CHECK(gfx::r_of(c) == 255 && gfx::g_of(c) == 240 && gfx::b_of(c) == 200);
    }
    // Sample beyond the radius: zero additive weight (alpha 0).
    {
        fx::Light L; L.x = 0; L.y = 0; L.radius = 50; L.intensity = 1.0f;
        CHECK(gfx::a_of(fx::light_sample(L, 100, 0)) == 0);
    }
    // Intensity scales the weight; a dimmer light adds less at the same point.
    {
        fx::Light bright; bright.x = 0; bright.y = 0; bright.radius = 100; bright.intensity = 1.0f;
        fx::Light dim = bright; dim.intensity = 0.5f;
        const int ab = gfx::a_of(fx::light_sample(bright, 40, 0));
        const int ad = gfx::a_of(fx::light_sample(dim,    40, 0));
        CHECK(ad < ab && ad > 0);
    }
    // Intensity > 1 saturates alpha at 255 rather than overflowing.
    {
        fx::Light hot; hot.x = 0; hot.y = 0; hot.radius = 100; hot.intensity = 4.0f;
        CHECK(gfx::a_of(fx::light_sample(hot, 30, 0)) == 255);
    }

    if (g_failures == 0) std::printf("light: all tests passed\n");
    else                 std::printf("light: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
