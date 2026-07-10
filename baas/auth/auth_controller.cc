// =============================================================================
//  baas/auth/auth_controller.cc  —  see auth_controller.h
// =============================================================================
#include "baas/auth/auth_controller.h"

#include <exception>
#include <optional>

#include <json/json.h>

#include "baas/app_config.h"
#include "baas/auth/auth_service.h"
#include "baas/auth/jwt.h"
#include "baas/common/context_keys.h"
#include "baas/common/errors.h"

namespace web {
namespace {

// Success shape shared by register/login/guest: {user:{...}, access_token:"..."}.
drogon::HttpResponsePtr auth_ok(const auth::User& u, long project_id) {
    const std::string token =
        jwt::issue(u.id, project_id, config().jwt_secret, config().jwt_ttl_seconds);
    Json::Value user;
    user["user_id"]      = static_cast<Json::Int64>(u.id);
    user["display_name"] = u.display_name;
    user["is_guest"]     = u.is_guest;
    Json::Value j;
    j["user"]         = user;
    j["access_token"] = token;
    return drogon::HttpResponse::newHttpJsonResponse(j);
}

// Turn a service Result into the right HTTP response.
drogon::HttpResponsePtr respond(const auth::Result& r, long project_id) {
    if (r.error) return make_error(r.error->status, r.error->code, r.error->message);
    return auth_ok(*r.user, project_id);
}

}  // namespace

void AuthController::reg(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long  pid  = req->attributes()->get<long>(kProjectId);
    const auto  body = req->getJsonObject();
    if (!body) { cb(make_error(400, "invalid_json", "expected a JSON body")); return; }
    try {
        auto r = auth::register_user(pid, (*body)["email"].asString(),
                                     (*body)["password"].asString(),
                                     (*body).get("display_name", "").asString());
        cb(respond(r, pid));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "registration failed"));
    }
}

void AuthController::login(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long  pid  = req->attributes()->get<long>(kProjectId);
    const auto  body = req->getJsonObject();
    if (!body) { cb(make_error(400, "invalid_json", "expected a JSON body")); return; }
    try {
        auto r = auth::login(pid, (*body)["email"].asString(),
                             (*body)["password"].asString());
        cb(respond(r, pid));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "login failed"));
    }
}

void AuthController::guest(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long  pid  = req->attributes()->get<long>(kProjectId);
    const auto  body = req->getJsonObject();   // optional body: {display_name?}
    const std::string name = body ? (*body).get("display_name", "").asString() : "";
    try {
        cb(respond(auth::guest(pid, name), pid));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "guest creation failed"));
    }
}

}  // namespace web
