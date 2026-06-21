// =============================================================================
//  tests/test_jobs.cpp  —  job system: pool, counters, parallel_for, sync path
// =============================================================================
#include "engine/jobs/job_system.hpp"

#include <atomic>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static void test_parallel_for() {
    jobs::JobSystem js;
    CHECK(js.worker_count() >= 0);

    constexpr std::size_t N = 10000;
    std::vector<int>      v(N, 0);
    js.parallel_for(N, [&](std::size_t i) { v[i] = static_cast<int>(i) * 2; }, 64);

    long sum = 0;
    for (std::size_t i = 0; i < N; ++i) {
        CHECK(v[i] == static_cast<int>(i) * 2);     // each slot written exactly once
        sum += v[i];
    }
    long expect = 0;
    for (std::size_t i = 0; i < N; ++i) expect += static_cast<long>(i) * 2;
    CHECK(sum == expect);
}

static void test_run_wait() {
    jobs::JobSystem js;
    std::atomic<int> n{0};
    jobs::Counter    c;
    for (int i = 0; i < 2000; ++i) js.run(c, [&] { n.fetch_add(1); });
    js.wait(c);
    CHECK(n.load() == 2000);                          // every counted job ran
}

static void test_wait_idle() {
    jobs::JobSystem js;
    std::atomic<int> k{0};
    for (int i = 0; i < 1000; ++i) js.run([&] { k.fetch_add(1); });   // fire-and-forget
    js.wait_idle();
    CHECK(k.load() == 1000);
}

static void test_nested() {
    jobs::JobSystem js;
    std::atomic<int> n{0};
    jobs::Counter    outer;
    js.run(outer, [&] {
        jobs::Counter inner;
        for (int i = 0; i < 10; ++i) js.run(inner, [&] { n.fetch_add(1); });
        js.wait(inner);                              // a job waiting on child jobs…
        n.fetch_add(100);
    });
    js.wait(outer);
    CHECK(n.load() == 110);                          // …completes without deadlock
}

static void test_single_worker_nested() {
    // With ONE worker, a job that waits on its children can only make progress because
    // wait() helps run queued jobs on the calling thread — this proves no deadlock.
    jobs::JobSystem js(1);
    CHECK(js.worker_count() == 1);
    std::atomic<int> n{0};
    jobs::Counter    outer;
    js.run(outer, [&] {
        jobs::Counter inner;
        for (int i = 0; i < 8; ++i) js.run(inner, [&] { n.fetch_add(1); });
        js.wait(inner);
        n.fetch_add(100);
    });
    js.wait(outer);
    CHECK(n.load() == 108);
}

static void test_synchronous() {
    jobs::JobSystem js(0);                            // forced synchronous (the web path)
    CHECK(js.worker_count() == 0);

    std::vector<int> v(100, -1);
    js.parallel_for(100, [&](std::size_t i) { v[i] = static_cast<int>(i); });
    CHECK(v[0] == 0 && v[99] == 99);

    std::atomic<int> m{0};
    jobs::Counter    c;
    js.run(c, [&] { m.fetch_add(5); });              // runs inline, immediately
    js.wait(c);                                       // no-op
    CHECK(m.load() == 5);
}

int main() {
    test_parallel_for();
    test_run_wait();
    test_wait_idle();
    test_nested();
    test_single_worker_nested();
    test_synchronous();
    if (g_failures == 0) std::printf("jobs: all tests passed\n");
    else                 std::printf("jobs: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
