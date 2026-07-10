// =============================================================================
//  baas/cloud_save/save_controller.h  —  HTTP edge for /v1/saves
// =============================================================================
//  All routes require api-key + JWT; the user comes from the token (never the
//  body), so a player only ever touches their own saves. {slot} is a path param.
// =============================================================================
#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace web {

class SaveController : public drogon::HttpController<SaveController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SaveController::put,    "/v1/saves/{slot}", drogon::Put,    "web::ApiKeyFilter", "web::AuthFilter");
    ADD_METHOD_TO(SaveController::get,    "/v1/saves/{slot}", drogon::Get,    "web::ApiKeyFilter", "web::AuthFilter");
    ADD_METHOD_TO(SaveController::remove, "/v1/saves/{slot}", drogon::Delete, "web::ApiKeyFilter", "web::AuthFilter");
    ADD_METHOD_TO(SaveController::list,   "/v1/saves",        drogon::Get,    "web::ApiKeyFilter", "web::AuthFilter");
    METHOD_LIST_END

    void put(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string slot);
    void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string slot);
    void remove(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string slot);
    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& cb);
};

}  // namespace web
