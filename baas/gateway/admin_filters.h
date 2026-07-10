// =============================================================================
//  baas/gateway/admin_filters.h  —  admin authentication (two levels)
// =============================================================================
//  AdminSecretFilter — platform admin: X-Admin-Secret must equal the configured
//    BAAS_ADMIN_SECRET (gates project creation/listing, before any project exists).
//  SecretKeyFilter — project admin: X-Secret-Key must verify (argon2) against the
//    project's secret_key_hash. Runs AFTER ApiKeyFilter (reads kProjectId).
//  Reference by "web::AdminSecretFilter" / "web::SecretKeyFilter".
// =============================================================================
#pragma once

#include <drogon/HttpFilter.h>

namespace web {

class AdminSecretFilter : public drogon::HttpFilter<AdminSecretFilter> {
public:
    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override;
};

class SecretKeyFilter : public drogon::HttpFilter<SecretKeyFilter> {
public:
    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override;
};

}  // namespace web
