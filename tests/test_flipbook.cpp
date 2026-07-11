// =============================================================================
//  tests/test_flipbook.cpp  —  sprite-sheet frame playback (dependency-free)
// =============================================================================
//  frame() steps at fps, stays in range, loops (with bounded t) or holds on the
//  last frame one-shot; done() flips only for one-shot. No SDL, no IO.
// =============================================================================
#include "engine/anim/flipbook.hpp"

#include <cstdio>

static int g_failures = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_failures; } \
    } while (0)

int main() {
    // A single-frame sheet is always frame 0.
    {
        anim::Flipbook fb{1, 8.0f, true};
        fb.update(10.0f);
        CHECK(fb.frame() == 0);
        CHECK(!fb.done());
    }

    // 8 frames @ 8 fps → one frame per 0.125 s.
    {
        anim::Flipbook fb{8, 8.0f, /*loop=*/true};
        CHECK(fb.frame() == 0);
        fb.update(0.125f); CHECK(fb.frame() == 1);
        fb.update(0.125f); CHECK(fb.frame() == 2);
        for (int i = 0; i < 5; ++i) fb.update(0.125f);   // → frame 7
        CHECK(fb.frame() == 7);
        fb.update(0.125f);                                // wraps back to 0
        CHECK(fb.frame() == 0);
        CHECK(!fb.done());
    }

    // frame() always stays within [0, frames-1] across a long run, and t stays
    // bounded (the loop period) rather than growing without limit.
    {
        anim::Flipbook fb{6, 12.0f, true};
        for (int i = 0; i < 1000; ++i) {
            fb.update(0.03f);
            const int f = fb.frame();
            CHECK(f >= 0 && f < 6);
        }
        CHECK(fb.t < 6.0f / 12.0f + 1e-4f);              // within one period
    }

    // One-shot: advances, holds on the last frame, done() flips.
    {
        anim::Flipbook fb{4, 10.0f, /*loop=*/false};     // 0.4 s total
        CHECK(fb.frame() == 0 && !fb.done());
        fb.update(0.1f); CHECK(fb.frame() == 1);
        fb.update(0.25f); CHECK(fb.frame() == 3);        // t=0.35 → frame 3
        CHECK(!fb.done());
        fb.update(0.1f);                                  // t=0.45 → past the end
        CHECK(fb.frame() == 3);                           // holds on last
        CHECK(fb.done());
        fb.update(5.0f);                                  // stays done + held
        CHECK(fb.frame() == 3 && fb.done());
    }

    // reset() rewinds.
    {
        anim::Flipbook fb{4, 10.0f, false};
        fb.update(1.0f);
        CHECK(fb.done());
        fb.reset();
        CHECK(fb.frame() == 0 && !fb.done());
    }

    if (g_failures == 0) std::printf("flipbook: all tests passed\n");
    else                 std::printf("flipbook: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
