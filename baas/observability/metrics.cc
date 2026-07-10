// =============================================================================
//  baas/observability/metrics.cc  —  see metrics.h
// =============================================================================
#include "baas/observability/metrics.h"

namespace web {

Metrics& Metrics::instance() {
    static Metrics m;
    return m;
}

std::string Metrics::normalize_path(const std::string& path) {
    std::string out;
    int         segs = 0;
    for (std::size_t i = 0; i < path.size() && segs < 2;) {
        if (path[i] == '/') { ++i; continue; }
        std::size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        out += "/" + path.substr(i, j - i);
        ++segs;
        i = j;
    }
    return out.empty() ? "/" : out;
}

void Metrics::record(const std::string& path, int status) {
    const char* cls = status >= 200 && status < 300   ? "2xx"
                      : status >= 300 && status < 400 ? "3xx"
                      : status >= 400 && status < 500 ? "4xx"
                      : status >= 500                 ? "5xx"
                                                      : "other";
    std::lock_guard<std::mutex> lk(mu_);
    ++total_;
    ++by_status_[cls];
    ++by_path_[normalize_path(path)];
}

MetricsSnapshot Metrics::snapshot() {
    std::lock_guard<std::mutex> lk(mu_);
    return MetricsSnapshot{total_, by_status_, by_path_};
}

void Metrics::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    total_ = 0;
    by_status_.clear();
    by_path_.clear();
}

}  // namespace web
