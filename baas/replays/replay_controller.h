// =============================================================================
//  baas/replays/replay_controller.h  —  HTTP edge for /v1/replays
// =============================================================================
//  All routes require api-key + JWT; the user comes from the token (never the
//  body), so a player only ever touches their own replays. {id} is a path param.
// =============================================================================
#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace web {

class ReplayController : public drogon::HttpController<ReplayController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ReplayController::create, "/v1/replays",      drogon::Post,   "web::ApiKeyFilter", "web::AuthFilter");
    ADD_METHOD_TO(ReplayController::list,   "/v1/replays",      drogon::Get,    "web::ApiKeyFilter", "web::AuthFilter");
    ADD_METHOD_TO(ReplayController::get,    "/v1/replays/{id}", drogon::Get,    "web::ApiKeyFilter", "web::AuthFilter");
    ADD_METHOD_TO(ReplayController::remove, "/v1/replays/{id}", drogon::Delete, "web::ApiKeyFilter", "web::AuthFilter");
    METHOD_LIST_END

    void create(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string id);
    void remove(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string id);
};

}  // namespace web
