// =============================================================================
//  tests/test_tween.cpp  —  easing curves + Tween (dependency-free, CTest)
// =============================================================================
//  Verifies every easing curve pins its endpoints, that ease() clamps its input,
//  and that Tween advances/holds/ping-pongs deterministically. No SDL, no IO.
// =============================================================================
#include "engine/anim/tween.hpp"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_failures; } \
    } while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

int main() {
    using anim::Ease;

    // Every curve pins f(0)=0 and f(1)=1 (Back/Elastic overshoot in between only).
    const Ease all[] = {
        Ease::Linear, Ease::SmoothStep, Ease::QuadIn, Ease::QuadOut, Ease::QuadInOut,
        Ease::CubicIn, Ease::CubicOut, Ease::CubicInOut, Ease::SineInOut, Ease::ExpoOut,
        Ease::BackOut, Ease::ElasticOut, Ease::BounceOut,
    };
    for (Ease e : all) {
        CHECK(near(anim::ease(e, 0.0f), 0.0f));
        CHECK(near(anim::ease(e, 1.0f), 1.0f));
    }

    // ease() clamps out-of-range progress rather than extrapolating.
    CHECK(anim::ease(Ease::Linear, -0.5f) == 0.0f);
    CHECK(anim::ease(Ease::Linear,  1.5f) == 1.0f);

    // A couple of known midpoints.
    CHECK(near(anim::ease(Ease::Linear,     0.5f), 0.5f));
    CHECK(near(anim::ease(Ease::SmoothStep, 0.5f), 0.5f));
    CHECK(near(anim::ease(Ease::QuadIn,     0.5f), 0.25f));

    // lerp basics.
    CHECK(near(anim::lerp(10.0f, 20.0f, 0.0f), 10.0f));
    CHECK(near(anim::lerp(10.0f, 20.0f, 1.0f), 20.0f));
    CHECK(near(anim::lerp(10.0f, 20.0f, 0.5f), 15.0f));

    // One-shot tween: start at from, saturate at to, done() flips.
    {
        anim::Tween t{100.0f, 200.0f, 1.0f, Ease::Linear};
        CHECK(near(t.value(), 100.0f));
        CHECK(!t.done());
        t.update(0.5f);
        CHECK(near(t.value(), 150.0f));
        CHECK(!t.done());
        t.update(0.5f);
        CHECK(near(t.value(), 200.0f));
        CHECK(t.done());
        t.update(10.0f);                       // overshoot time: stays pinned to `to`
        CHECK(near(t.value(), 200.0f));
        CHECK(t.done());
    }

    // Ping-pong: returns to `from` after a full round trip, never reports done,
    // and does not drift after many periods.
    {
        anim::Tween t{0.0f, 10.0f, 1.0f, Ease::Linear, /*pingpong=*/true};
        t.update(1.0f);                        // reached `to`, flipped to reversing
        CHECK(near(t.value(), 10.0f));
        CHECK(!t.done());
        t.update(1.0f);                        // reversed all the way back to `from`
        CHECK(near(t.value(), 0.0f));
        CHECK(!t.done());
        t.update(5.0f);                        // 5 more full legs, no drift
        CHECK(!t.done());
        const float v = t.value();
        CHECK(v >= 0.0f && v <= 10.0f);
    }

    // Determinism: identical tweens fed the same dt stream stay in lockstep.
    {
        anim::Tween a{0.0f, 1.0f, 2.0f, Ease::CubicInOut};
        anim::Tween b = a;
        for (int i = 0; i < 30; ++i) { a.update(0.1f); b.update(0.1f); CHECK(a.value() == b.value()); }
    }

    // reset() rewinds a finished tween to the start.
    {
        anim::Tween t{0.0f, 1.0f, 1.0f, Ease::Linear};
        t.update(2.0f);
        CHECK(t.done());
        t.reset();
        CHECK(!t.done());
        CHECK(near(t.value(), 0.0f));
    }

    if (g_failures == 0) std::printf("tween: all tests passed\n");
    else                 std::printf("tween: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
