// =============================================================================
//  baas/gateway/rate_limiter.h  —  a thread-safe token-bucket rate limiter
// =============================================================================
//  Keyed by an arbitrary string (an api-key, or "ip:<addr>" for keyless requests).
//  Each key gets a bucket that holds up to `capacity` tokens and refills at
//  `refill_per_sec`; each allowed request spends one token. A burst up to capacity
//  is fine; sustained traffic is capped at the refill rate.
//
//  It is PURE with respect to time — the caller passes `now` (seconds) — so the
//  logic is deterministically unit-testable without sleeping. The gateway advice
//  supplies a monotonic clock; tests supply their own timeline.
//
//  ponytail: one mutex + an unbounded map keyed by caller. Fine for a single node;
//  a huge key space would want sharding + eviction of idle buckets. Stated, not
//  built.
// =============================================================================
#pragma once

#include <map>
#include <mutex>
#include <string>

namespace web {

class RateLimiter {
public:
    RateLimiter(double capacity, double refill_per_sec);

    // Consume one token for `key` at time `now_seconds`. Returns false if the
    // bucket is empty (the request should be rejected with 429).
    bool allow(const std::string& key, double now_seconds);

    void reset();   // drop all buckets (test isolation)

private:
    struct Bucket {
        double tokens = 0;
        double last   = 0;   // last refill time (seconds)
    };

    double                        capacity_;
    double                        refill_;
    std::mutex                    mu_;
    std::map<std::string, Bucket> buckets_;
};

}  // namespace web
