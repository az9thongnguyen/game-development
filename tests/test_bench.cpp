// =============================================================================
//  tests/test_bench.cpp  —  the arithmetic behind a frame-cost number
// =============================================================================
//  `--bench-ui` was sixty lines inside `src/main.cpp` for thirty-six chapters, and the
//  percentile arithmetic inside it had never been asked a single question — because
//  asking one meant allocating a 14 MB framebuffer and rendering the Studio. It was
//  wrong in two ways at once, and the first is why this file exists:
//
//    * `--bench-ui 0` segfaulted: `ms[ms.size() / 2]` on an empty vector. `atoi` turns
//      any typo into 0, so a mistyped argument crashed the tool.
//    * `ms[size * 95 / 100]` is integer arithmetic on the INDEX. For ten samples that is
//      index 9 — the WORST frame of the run, printed as its 95th percentile.
//
//  Neither needs a clock to find. Both need the arithmetic to be reachable without one.
// =============================================================================
#include "engine/bench.hpp"

#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

static void test_nothing_measured_is_not_a_crash() {
    const bench::Summary s = bench::summarize({});
    CHECK(s.frames == 0);
    CHECK(s.median == 0.0);
    CHECK(s.p95 == 0.0);
    CHECK(s.best == 0.0 && s.worst == 0.0);
    // ...and the percentile helper agrees rather than reading off the end.
    CHECK(bench::percentile({}, 0.5) == 0.0);
    CHECK(bench::percentile({}, 0.95) == 0.0);
}

static void test_one_sample_is_every_percentile() {
    const bench::Summary s = bench::summarize({4.0});
    CHECK(s.frames == 1);
    CHECK(s.median == 4.0);
    CHECK(s.p95 == 4.0);
    CHECK(s.best == 4.0 && s.worst == 4.0);
}

static void test_the_percentile_is_nearest_rank() {
    // Ten samples, 1..10. p95 is the 10th (ceil(0.95 * 10) == 10) and p50 the 5th.
    std::vector<double> ten;
    for (int i = 1; i <= 10; ++i) ten.push_back(static_cast<double>(i));
    CHECK(bench::percentile(ten, 0.50) == 5.0);
    CHECK(bench::percentile(ten, 0.95) == 10.0);
    CHECK(bench::percentile(ten, 0.10) == 1.0);

    // Twenty samples: p95 is the 19th, NOT the worst. This is the case the old
    // `size * 95 / 100` got right by accident and the ten-sample case above got wrong —
    // an index formula that agrees with the correct one on the sizes you happen to try.
    std::vector<double> twenty;
    for (int i = 1; i <= 20; ++i) twenty.push_back(static_cast<double>(i));
    CHECK(bench::percentile(twenty, 0.95) == 19.0);
    CHECK(bench::percentile(twenty, 1.0) == 20.0);
    CHECK(bench::percentile(twenty, 0.0) == 1.0);

    // Out of range in both directions, which is what the two clamps are FOR. They were
    // dead when the function also had early-outs for p<=0 and p>=1 — the pair covered
    // each other, and four mutations survived saying so. These two lines are what makes
    // the survivors killable, and they are also the only reason the clamps exist.
    CHECK(bench::percentile(twenty, -1.0) == 1.0);
    CHECK(bench::percentile(twenty, 2.0) == 20.0);
    CHECK(bench::percentile({7.0, 8.0}, -0.001) == 7.0);
    CHECK(bench::percentile({7.0, 8.0}, 1.001) == 8.0);
}

static void test_summarize_sorts_and_does_not_disturb_the_caller() {
    std::vector<double> mixed{9.0, 1.0, 5.0, 3.0, 7.0};
    const std::vector<double> before = mixed;
    const bench::Summary s = bench::summarize(mixed);
    CHECK(s.frames == 5);
    CHECK(s.best == 1.0);
    CHECK(s.worst == 9.0);
    CHECK(s.median == 5.0);
    // Taken BY VALUE: the caller's vector is untouched. A benchmark that reordered the
    // samples it was handed would be a surprise nobody would look for.
    CHECK(mixed == before);
}

// The loop's honest claim: it produces the number of samples it was asked for, it does
// not crash on a degenerate size, and it never touches a clock the test depends on.
static void test_the_loop_reports_what_it_ran() {
    struct Blank : engine::Scene {
        int drawn = 0;
        void render(const engine::Context&) override { ++drawn; }
    };
    {
        Blank sc;
        const bench::Summary s = bench::run(sc, bench::Config{64, 32, 1, 7, 3}, nullptr);
        CHECK(s.frames == 7);
        CHECK(sc.drawn == 10);           // 3 warm-up frames are rendered and NOT timed
        CHECK(s.best <= s.median);
        CHECK(s.median <= s.worst);
    }
    {
        Blank sc;
        const bench::Summary s = bench::run(sc, bench::Config{64, 32, 1, 0, 0}, nullptr);
        CHECK(s.frames == 0);            // and asking for none renders none
        CHECK(sc.drawn == 0);
    }
    {
        // A size that cannot be allocated is refused instead of multiplied out into a
        // negative pixel count.
        Blank sc;
        CHECK(bench::run(sc, bench::Config{0, 32, 1, 4, 0}, nullptr).frames == 0);
        CHECK(bench::run(sc, bench::Config{64, 32, 0, 4, 0}, nullptr).frames == 0);
        CHECK(sc.drawn == 0);
    }
}

int main() {
    test_nothing_measured_is_not_a_crash();
    test_one_sample_is_every_percentile();
    test_the_percentile_is_nearest_rank();
    test_summarize_sorts_and_does_not_disturb_the_caller();
    test_the_loop_reports_what_it_ran();
    if (g_failures == 0) std::printf("bench: all tests passed\n");
    else                 std::printf("bench: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
