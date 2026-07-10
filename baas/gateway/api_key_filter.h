// =============================================================================
//  baas/gateway/api_key_filter.h  —  the multi-tenancy gate
// =============================================================================
//  Runs before every /v1 handler: turns the client's X-Api-Key into a project_id
//  and attaches it to the request (attribute kProjectId). A missing/unknown key
//  is a 401 — nothing downstream ever runs without a resolved project. Reference
//  it in a route's filter list by its FULLY-QUALIFIED name "web::ApiKeyFilter".
// =============================================================================
#pragma once

#include <drogon/HttpFilter.h>

namespace web {

class ApiKeyFilter : public drogon::HttpFilter<ApiKeyFilter> {
public:
    void doFilter(const drogon::HttpRequestPtr& req,
                  drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override;
};

}  // namespace web
