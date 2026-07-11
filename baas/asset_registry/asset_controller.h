// =============================================================================
//  baas/asset_registry/asset_controller.h  —  HTTP edge for /v1/assets
// =============================================================================
//  Project-scoped (not per-user): routes require ONLY the api-key gate
//  (ApiKeyFilter → kProjectId), NOT the per-user JWT. Every player of a game
//  reads the same assets; the operator writes them. {name} is a path param.
// =============================================================================
#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace web {

class AssetController : public drogon::HttpController<AssetController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AssetController::put,    "/v1/assets/{name}", drogon::Put,    "web::ApiKeyFilter");
    ADD_METHOD_TO(AssetController::get,    "/v1/assets/{name}", drogon::Get,    "web::ApiKeyFilter");
    ADD_METHOD_TO(AssetController::remove, "/v1/assets/{name}", drogon::Delete, "web::ApiKeyFilter");
    ADD_METHOD_TO(AssetController::list,   "/v1/assets",        drogon::Get,    "web::ApiKeyFilter");
    METHOD_LIST_END

    void put(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string name);
    void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string name);
    void remove(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string name);
    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& cb);
};

}  // namespace web
