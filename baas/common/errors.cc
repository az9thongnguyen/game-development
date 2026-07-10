// =============================================================================
//  baas/common/errors.cc  —  see errors.h
// =============================================================================
#include "baas/common/errors.h"

#include <json/json.h>

namespace web {

drogon::HttpResponsePtr make_error(int status, const std::string& code,
                                   const std::string& message) {
    Json::Value err;
    err["code"]    = code;
    err["message"] = message;
    Json::Value body;
    body["error"] = err;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    // The numeric HTTP codes ARE the enum values, so the cast is well-defined
    // for any standard status we pass (400/401/403/404/405/409/500).
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
    return resp;
}

}  // namespace web
