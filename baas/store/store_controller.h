// =============================================================================
//  baas/store/store_controller.h  —  HTTP edge for /v1/store (client)
// =============================================================================
//  Reading the catalog needs only an api-key (prices are public); buying needs a
//  logged-in user (the JWT), because a purchase spends and grants against that user.
//  Defining offers is an admin action and lives in AdminController.
// =============================================================================
#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace web {

class StoreController : public drogon::HttpController<StoreController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(StoreController::catalog, "/v1/store/catalog",    drogon::Get,  "web::ApiKeyFilter");
    ADD_METHOD_TO(StoreController::buy,     "/v1/store/buy/{sku}",  drogon::Post, "web::ApiKeyFilter", "web::AuthFilter");
    METHOD_LIST_END

    void catalog(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void buy(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string sku);
};

}  // namespace web
