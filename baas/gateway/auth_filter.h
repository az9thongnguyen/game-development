// =============================================================================
//  baas/gateway/auth_filter.h  —  the "must be logged in" gate
// =============================================================================
//  Runs AFTER ApiKeyFilter (it reads the resolved kProjectId): verifies the
//  Bearer JWT, checks the token's project matches the request's project, and
//  attaches kUserId. Reference it in a route's filter list AFTER
//  "web::ApiKeyFilter", by "web::AuthFilter".
// =============================================================================
#pragma once

#include <drogon/HttpFilter.h>

namespace web {

class AuthFilter : public drogon::HttpFilter<AuthFilter> {
public:
    void doFilter(const drogon::HttpRequestPtr& req,
                  drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override;
};

}  // namespace web
