// =============================================================================
//  baas/app_setup.cc  —  see app_setup.h
// =============================================================================
#include "baas/app_setup.h"

#include <functional>

#include <drogon/drogon.h>
#include <json/json.h>

#include "baas/common/context_keys.h"

namespace web {

void register_routes() {
    // Liveness probe — no auth, dependency-free; used by tests + orchestration.
    drogon::app().registerHandler(
        "/healthz",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            Json::Value j;
            j["status"] = "ok";
            cb(drogon::HttpResponse::newHttpJsonResponse(j));
        },
        {drogon::Get});

    // Authenticated liveness — behind the api-key gate; echoes the resolved
    // project so we can prove the ApiKeyFilter runs and attaches kProjectId.
    drogon::app().registerHandler(
        "/v1/ping",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            Json::Value j;
            j["project_id"] =
                static_cast<Json::Int64>(req->attributes()->get<long>(kProjectId));
            cb(drogon::HttpResponse::newHttpJsonResponse(j));
        },
        {drogon::Get, std::string("web::ApiKeyFilter")});
}

}  // namespace web
