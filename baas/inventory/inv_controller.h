// =============================================================================
//  baas/inventory/inv_controller.h  —  HTTP edge for /v1/inventory
// =============================================================================
//  All routes require api-key + JWT; the user comes from the token, so a player
//  only ever touches their own inventory. {item} is a path param.
// =============================================================================
#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace web {

class InventoryController : public drogon::HttpController<InventoryController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(InventoryController::list,    "/v1/inventory",                drogon::Get,  "web::ApiKeyFilter", "web::AuthFilter");
    ADD_METHOD_TO(InventoryController::get,     "/v1/inventory/{item}",         drogon::Get,  "web::ApiKeyFilter", "web::AuthFilter");
    ADD_METHOD_TO(InventoryController::grant,   "/v1/inventory/{item}/grant",   drogon::Post, "web::ApiKeyFilter", "web::AuthFilter");
    ADD_METHOD_TO(InventoryController::consume, "/v1/inventory/{item}/consume", drogon::Post, "web::ApiKeyFilter", "web::AuthFilter");
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string item);
    void grant(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string item);
    void consume(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string item);
};

}  // namespace web
