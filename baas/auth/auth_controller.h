// =============================================================================
//  baas/auth/auth_controller.h  —  HTTP edge for /v1/auth/*
// =============================================================================
//  Drogon HttpController: auto-registers its routes when linked (baas_core is an
//  OBJECT library so the registration runs). All routes sit behind ApiKeyFilter
//  (you need a valid project to create/authenticate a user, but NOT a prior
//  login). `register` is a C++ keyword, hence the method is named `reg`.
// =============================================================================
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace web {

class AuthController : public drogon::HttpController<AuthController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::reg,   "/v1/auth/register", drogon::Post, "web::ApiKeyFilter");
    ADD_METHOD_TO(AuthController::login, "/v1/auth/login",    drogon::Post, "web::ApiKeyFilter");
    ADD_METHOD_TO(AuthController::guest, "/v1/auth/guest",    drogon::Post, "web::ApiKeyFilter");
    METHOD_LIST_END

    void reg(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void login(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void guest(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& cb);
};

}  // namespace web
