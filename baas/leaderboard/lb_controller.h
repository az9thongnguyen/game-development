// =============================================================================
//  baas/leaderboard/lb_controller.h  —  HTTP edge for /v1/leaderboards/*
// =============================================================================
//  top   : GET  /v1/leaderboards/{key}/top?limit=N   (api-key only; public read)
//  submit: POST /v1/leaderboards/{key}/scores {value} (api-key + JWT)
//  me    : GET  /v1/leaderboards/{key}/me            (api-key + JWT)
//  Filter order matters: ApiKeyFilter first (sets project), then AuthFilter
//  (reads project, sets user). The {key} path param arrives as a trailing arg.
// =============================================================================
#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace web {

class LbController : public drogon::HttpController<LbController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(LbController::top,    "/v1/leaderboards/{key}/top",    drogon::Get,  "web::ApiKeyFilter");
    ADD_METHOD_TO(LbController::submit, "/v1/leaderboards/{key}/scores", drogon::Post, "web::ApiKeyFilter", "web::AuthFilter");
    ADD_METHOD_TO(LbController::me,     "/v1/leaderboards/{key}/me",     drogon::Get,  "web::ApiKeyFilter", "web::AuthFilter");
    METHOD_LIST_END

    void top(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string key);
    void submit(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string key);
    void me(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string key);
};

}  // namespace web
