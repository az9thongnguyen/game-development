// =============================================================================
//  tests/test_fixed_step.cpp  —  the clock that must not be copied
// =============================================================================
//  FixedStep came out of App::frame so the Play viewport could drive a scene on the
//  same clock. These tests are about the properties that make a SECOND copy dangerous:
//  the exact step count, the spiral-of-death clamp, and the leftover that carries into
//  the next frame. Two implementations agreeing on 1/60 s frames and disagreeing on a
//  stall is the whole failure mode.
// =============================================================================
#include "engine/fixed_step.hpp"

#include <cmath>
#include <cstdio>

using namespace engine;

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

int main() {
    constexpr double kDt = 1.0 / 60.0;

    // ---- a frame shorter than one step runs nothing, but is not LOST ---------
    // This is the bug the farm clock had in its own accumulator: truncating per call
    // makes time stop entirely when every frame is smaller than the unit.
    {
        FixedStep c(kDt);
        CHECK(c.advance(kDt / 3) == 0);
        CHECK(c.advance(kDt / 3) == 0);
        CHECK(c.advance(kDt / 3) == 1);       // the thirds added up
        CHECK(near(c.time(), kDt));
    }

    // ---- exact step counts ---------------------------------------------------
    {
        FixedStep c(kDt);
        CHECK(c.advance(kDt) == 1);
        CHECK(c.advance(kDt * 3) == 3);
        CHECK(c.advance(0.0) == 0);           // a zero frame advances nothing
        CHECK(near(c.time(), kDt * 4));
    }

    // ---- alpha is the leftover, in [0,1) ------------------------------------
    {
        FixedStep c(kDt);
        c.advance(kDt * 1.5);
        CHECK(c.alpha() >= 0.0 && c.alpha() < 1.0);
        CHECK(near(c.alpha(), 0.5, 1e-6));
    }

    // ---- the spiral-of-death clamp DROPS time, deliberately ------------------
    // A two-second stall must not queue 120 updates. It also must not silently
    // pretend two seconds passed: simulated time advances by the clamp, not the stall.
    {
        FixedStep c(kDt, 0.25);
        const int steps = c.advance(2.0);
        CHECK(steps == 15);                   // 0.25 / (1/60)
        CHECK(c.time() < 0.26);
        CHECK(c.time() > 0.24);
    }

    // ---- no drift over many frames ------------------------------------------
    // 600 frames of exactly one step each must be exactly 600 steps. A version that
    // compared with > instead of >= loses one step per frame to floating point and
    // this is where that shows up.
    {
        FixedStep c(kDt);
        int total = 0;
        for (int i = 0; i < 600; ++i) total += c.advance(kDt);
        CHECK(total == 600);
        CHECK(near(c.time(), 10.0, 1e-9));
    }

    // ---- ragged real frames still average out -------------------------------
    // Ten seconds of jittery frames must produce the same 600 steps as ten seconds of
    // perfect ones: that is the entire point of decoupling logic from the display.
    {
        FixedStep c(kDt);
        int total = 0;
        double fed = 0.0;
        const double jitter[] = {0.011, 0.019, 0.007, 0.031, 0.016};
        for (int i = 0; fed < 10.0; ++i) {
            const double d = jitter[i % 5];
            fed += d;
            total += c.advance(d);
        }
        // Every whole step inside the time fed, and nothing more.
        CHECK(total == static_cast<int>(fed / kDt) || total == static_cast<int>(fed / kDt) + 1);
        CHECK(near(c.time(), total * kDt, 1e-9));
    }

    // ---- reset() clears the leftover as well as the total --------------------
    // A Play viewport that restarts a game restarts its clock; leaving the fractional
    // remainder behind makes the first frame of the new run inherit the old one's.
    {
        FixedStep c(kDt);
        c.advance(kDt * 2.5);
        CHECK(c.alpha() > 0.4);
        c.reset();
        CHECK(near(c.time(), 0.0));
        CHECK(near(c.alpha(), 0.0));
        CHECK(c.advance(kDt * 0.9) == 0);     // ...so a short frame runs nothing again
    }

    if (g_failures == 0) std::printf("fixed_step: all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
