// =============================================================================
//  baas/gateway/admin_filters.cc  —  see admin_filters.h
// =============================================================================
#include "baas/gateway/admin_filters.h"

#include <exception>
#include <string>

#include <sodium.h>

#include "baas/app_config.h"
#include "baas/auth/password.h"
#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/db/db.h"

namespace web {

void AdminSecretFilter::doFilter(const drogon::HttpRequestPtr& req,
                                 drogon::FilterCallback&&      fcb,
                                 drogon::FilterChainCallback&& fccb) {
    const std::string got  = req->getHeader("x-admin-secret");
    const std::string want = config().admin_secret;
    // Constant-time compare of equal-length secrets (length check is not sensitive here).
    if (got.empty() || got.size() != want.size() ||
        sodium_memcmp(got.data(), want.data(), want.size()) != 0) {
        fcb(make_error(401, "unauthorized", "invalid admin secret"));
        return;
    }
    fccb();
}

void SecretKeyFilter::doFilter(const drogon::HttpRequestPtr& req,
                               drogon::FilterCallback&&      fcb,
                               drogon::FilterChainCallback&& fccb) {
    const long        pid    = req->attributes()->get<long>(kProjectId);   // set by ApiKeyFilter
    const std::string secret = req->getHeader("x-secret-key");
    if (secret.empty()) {
        fcb(make_error(401, "unauthorized", "missing X-Secret-Key"));
        return;
    }
    try {
        const auto rows =
            db::client()->execSqlSync("SELECT secret_key_hash FROM projects WHERE id=?", pid);
        if (rows.empty() || !pw::verify(secret, rows[0]["secret_key_hash"].as<std::string>())) {
            fcb(make_error(401, "unauthorized", "invalid secret key"));
            return;
        }
        fccb();
    } catch (const std::exception&) {
        fcb(make_error(500, "internal", "secret verification failed"));
    }
}

}  // namespace web
