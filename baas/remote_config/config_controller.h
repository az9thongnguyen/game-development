// =============================================================================
//  baas/remote_config/config_controller.h  —  HTTP edge for /v1/config (read)
// =============================================================================
//  Public read (api-key only — config is not per-user). Writing is a dashboard
//  (L3) admin action, not part of the client SDK.
// =============================================================================
#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace web {

class ConfigController : public drogon::HttpController<ConfigController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConfigController::all, "/v1/config",       drogon::Get, "web::ApiKeyFilter");
    ADD_METHOD_TO(ConfigController::get, "/v1/config/{key}", drogon::Get, "web::ApiKeyFilter");
    METHOD_LIST_END

    void all(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string key);
};

}  // namespace web
