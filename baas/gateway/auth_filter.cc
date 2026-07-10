// =============================================================================
//  baas/gateway/auth_filter.cc  —  see auth_filter.h
// =============================================================================
#include "baas/gateway/auth_filter.h"

#include "baas/app_config.h"
#include "baas/auth/jwt.h"
#include "baas/common/context_keys.h"
#include "baas/common/errors.h"

namespace web {

void AuthFilter::doFilter(const drogon::HttpRequestPtr& req,
                          drogon::FilterCallback&& fcb,
                          drogon::FilterChainCallback&& fccb) {
    const std::string header = req->getHeader("authorization");
    const std::string prefix = "Bearer ";
    if (header.rfind(prefix, 0) != 0) {
        fcb(make_error(401, "unauthorized", "missing Bearer token"));
        return;
    }
    const auto claims = jwt::verify(header.substr(prefix.size()), config().jwt_secret);
    if (!claims) {
        fcb(make_error(401, "unauthorized", "invalid or expired token"));
        return;
    }
    // The token must belong to the same project the api-key resolved to.
    if (claims->pid != req->attributes()->get<long>(kProjectId)) {
        fcb(make_error(401, "unauthorized", "token does not match project"));
        return;
    }
    req->attributes()->insert(kUserId, claims->sub);
    fccb();
}

}  // namespace web
