// =============================================================================
//  baas/gateway/api_key_filter.cc  —  see api_key_filter.h
// =============================================================================
#include "baas/gateway/api_key_filter.h"

#include <exception>

#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/db/db.h"

namespace web {

void ApiKeyFilter::doFilter(const drogon::HttpRequestPtr& req,
                            drogon::FilterCallback&& fcb,
                            drogon::FilterChainCallback&& fccb) {
    const std::string key = req->getHeader("x-api-key");   // getHeader is case-insensitive
    if (key.empty()) {
        fcb(make_error(401, "unauthorized", "missing X-Api-Key"));
        return;
    }
    try {
        // ponytail: execSqlSync blocks this event-loop thread; fine for SQLite +
        // demo load. Switch to execSqlAsync if request throughput ever demands it.
        const auto rows =
            db::client()->execSqlSync("SELECT id FROM projects WHERE public_key=?", key);
        if (rows.empty()) {
            fcb(make_error(401, "unauthorized", "invalid X-Api-Key"));
            return;
        }
        // Stored as long; controllers must read it back with get<long>(kProjectId).
        req->attributes()->insert(kProjectId, rows[0]["id"].as<long>());
        fccb();   // continue the chain
    } catch (const std::exception&) {
        fcb(make_error(500, "internal", "project lookup failed"));
    }
}

}  // namespace web
