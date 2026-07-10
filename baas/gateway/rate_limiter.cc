// =============================================================================
//  baas/gateway/rate_limiter.cc  —  see rate_limiter.h
// =============================================================================
#include "baas/gateway/rate_limiter.h"

#include <algorithm>

namespace web {

RateLimiter::RateLimiter(double capacity, double refill_per_sec)
    : capacity_(capacity), refill_(refill_per_sec) {}

bool RateLimiter::allow(const std::string& key, double now_seconds) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        // First request from this key: a full bucket, minus the one we spend now.
        buckets_[key] = Bucket{capacity_ - 1.0, now_seconds};
        return capacity_ >= 1.0;
    }
    Bucket&      b       = it->second;
    const double elapsed = now_seconds - b.last;
    if (elapsed > 0) {
        b.tokens = std::min(capacity_, b.tokens + elapsed * refill_);
        b.last   = now_seconds;
    }
    if (b.tokens >= 1.0) {
        b.tokens -= 1.0;
        return true;
    }
    return false;
}

void RateLimiter::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    buckets_.clear();
}

}  // namespace web
