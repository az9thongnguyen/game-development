// =============================================================================
//  baas/observability/metrics.h  —  in-process request metrics
// =============================================================================
//  A tiny, thread-safe counter store the gateway feeds from a pre-sending advice
//  (one record() per response) and the /metrics endpoint reads. It tracks the total
//  request count, a tally by HTTP status class (2xx/3xx/4xx/5xx/other), and a tally
//  by NORMALIZED route (path collapsed to its first two segments, so per-id routes
//  don't explode the key space).
//
//  ponytail: process-local counters. Across a fleet each node has its own; a real
//  deploy scrapes every node and sums (Prometheus-style) or ships to a TSDB. Stated,
//  not built.
// =============================================================================
#pragma once

#include <map>
#include <mutex>
#include <string>

namespace web {

struct MetricsSnapshot {
    long long                          total = 0;
    std::map<std::string, long long>   by_status;   // "2xx" → count
    std::map<std::string, long long>   by_path;     // "/v1/replays" → count
};

class Metrics {
public:
    static Metrics& instance();

    // Record one completed response. `path` is the raw request path; it is
    // normalized to its route group internally.
    void record(const std::string& path, int status);

    MetricsSnapshot snapshot();
    void            reset();   // test isolation

    // Collapse a path to its first two non-empty segments (bounds cardinality):
    //   "/v1/replays/42" → "/v1/replays"; "/healthz" → "/healthz".
    static std::string normalize_path(const std::string& path);

private:
    Metrics() = default;
    std::mutex                       mu_;
    long long                        total_ = 0;
    std::map<std::string, long long> by_status_;
    std::map<std::string, long long> by_path_;
};

}  // namespace web
