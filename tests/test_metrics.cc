// =============================================================================
//  tests/test_metrics.cc  —  unit tests for the request-metrics counters
// =============================================================================
//  Pure logic, no server: path normalization (route grouping / cardinality bound),
//  the status-class tally, per-route counts, and reset.
// =============================================================================
#include <cstdio>

#include "baas/observability/metrics.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

int main() {
    using web::Metrics;

    // --- path normalization collapses per-id routes ---
    CHECK(Metrics::normalize_path("/v1/replays/42") == "/v1/replays");
    CHECK(Metrics::normalize_path("/v1/leaderboards/colony_high/top") == "/v1/leaderboards");
    CHECK(Metrics::normalize_path("/healthz") == "/healthz");
    CHECK(Metrics::normalize_path("/") == "/");
    CHECK(Metrics::normalize_path("") == "/");

    // --- record + snapshot ---
    Metrics& m = Metrics::instance();
    m.reset();
    m.record("/v1/replays/1", 200);
    m.record("/v1/replays/2", 201);
    m.record("/v1/auth/guest", 401);
    m.record("/v1/ping", 500);
    m.record("/healthz", 302);
    const auto s = m.snapshot();
    CHECK(s.total == 5);
    CHECK(s.by_status.at("2xx") == 2);   // 200, 201
    CHECK(s.by_status.at("3xx") == 1);   // 302
    CHECK(s.by_status.at("4xx") == 1);   // 401
    CHECK(s.by_status.at("5xx") == 1);   // 500
    CHECK(s.by_path.at("/v1/replays") == 2);
    CHECK(s.by_path.at("/healthz") == 1);

    // --- reset clears everything ---
    m.reset();
    CHECK(m.snapshot().total == 0);
    CHECK(m.snapshot().by_status.empty());

    if (g_failures == 0) std::printf("metrics: all tests passed\n");
    else                 std::printf("metrics: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
