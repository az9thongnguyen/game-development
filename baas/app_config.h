// =============================================================================
//  baas/app_config.h  —  process-wide runtime configuration
// =============================================================================
//  Holds secrets/settings that controllers (constructed by Drogon) need but can't
//  be handed directly. Set once at startup from CLI/env; read via config().
//  The JWT secret must come from the environment in any real deployment.
// =============================================================================
#pragma once

#include <string>

namespace web {

struct AppConfig {
    std::string jwt_secret;              // HS256 signing key (from BAAS_JWT_SECRET)
    int         jwt_ttl_seconds = 3600;  // access-token lifetime
    std::string admin_secret = {};       // platform-admin secret (from BAAS_ADMIN_SECRET)

    // Per-caller (api-key, else IP) token-bucket rate limit on /v1/* routes.
    // rate_capacity <= 0 disables rate limiting entirely.
    double      rate_capacity      = 0;   // burst size (tokens)
    double      rate_refill_per_sec = 0;  // sustained refill rate
};

void             set_config(AppConfig cfg);
const AppConfig& config();

}  // namespace web
