// =============================================================================
//  tests/test_rate_limiter.cc  —  unit tests for the token-bucket rate limiter
// =============================================================================
//  Pure logic, deterministic: time is injected, so no sleeping. Covers the initial
//  burst, refill over time, the capacity ceiling, per-key independence, reset, and
//  the disabled (capacity 0) case.
// =============================================================================
#include <cstdio>

#include "baas/gateway/rate_limiter.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

int main() {
    // capacity 3, refill 1 token/sec
    web::RateLimiter rl(3, 1.0);

    // --- initial burst: 3 allowed at t=0, the 4th throttled ---
    CHECK(rl.allow("a", 0.0));
    CHECK(rl.allow("a", 0.0));
    CHECK(rl.allow("a", 0.0));
    CHECK(!rl.allow("a", 0.0));

    // --- refill: 2s later, 2 tokens are back → 2 allowed then throttled ---
    CHECK(rl.allow("a", 2.0));
    CHECK(rl.allow("a", 2.0));
    CHECK(!rl.allow("a", 2.0));

    // --- ceiling: after a long idle, tokens cap at capacity (3), not 3+idle ---
    CHECK(rl.allow("a", 100.0));
    CHECK(rl.allow("a", 100.0));
    CHECK(rl.allow("a", 100.0));
    CHECK(!rl.allow("a", 100.0));   // capped at 3

    // --- per-key independence: key "b" has its own full bucket ---
    CHECK(rl.allow("b", 100.0));
    CHECK(rl.allow("b", 100.0));
    CHECK(rl.allow("b", 100.0));
    CHECK(!rl.allow("b", 100.0));

    // --- reset drops all buckets ---
    rl.reset();
    CHECK(rl.allow("a", 100.0));

    // --- capacity 0 → everything throttled (the "disabled" sentinel) ---
    web::RateLimiter z(0, 0);
    CHECK(!z.allow("x", 0.0));

    if (g_failures == 0) std::printf("rate_limiter: all tests passed\n");
    else                 std::printf("rate_limiter: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
