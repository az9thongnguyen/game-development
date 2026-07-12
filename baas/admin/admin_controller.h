// =============================================================================
//  baas/admin/admin_controller.h  —  HTTP edge for /v1/admin/* (the dashboard API)
// =============================================================================
//  Two auth levels: project creation/listing needs the platform admin secret
//  (AdminSecretFilter); everything per-project needs the project's secret key
//  (ApiKeyFilter → project, then SecretKeyFilter).
// =============================================================================
#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace web {

class AdminController : public drogon::HttpController<AdminController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AdminController::createProject, "/v1/admin/projects", drogon::Post, "web::AdminSecretFilter");
    ADD_METHOD_TO(AdminController::listProjects,  "/v1/admin/projects", drogon::Get,  "web::AdminSecretFilter");
    ADD_METHOD_TO(AdminController::setConfig,    "/v1/admin/config/{key}", drogon::Put,    "web::ApiKeyFilter", "web::SecretKeyFilter");
    ADD_METHOD_TO(AdminController::deleteConfig, "/v1/admin/config/{key}", drogon::Delete, "web::ApiKeyFilter", "web::SecretKeyFilter");
    ADD_METHOD_TO(AdminController::createEvent,  "/v1/admin/events",          drogon::Post, "web::ApiKeyFilter", "web::SecretKeyFilter");
    ADD_METHOD_TO(AdminController::analytics,    "/v1/admin/analytics/summary", drogon::Get, "web::ApiKeyFilter", "web::SecretKeyFilter");
    ADD_METHOD_TO(AdminController::listUsers,    "/v1/admin/users",           drogon::Get,  "web::ApiKeyFilter", "web::SecretKeyFilter");
    ADD_METHOD_TO(AdminController::rotateSecret, "/v1/admin/secret/rotate",   drogon::Post, "web::ApiKeyFilter", "web::SecretKeyFilter");
    METHOD_LIST_END

    void createProject(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void listProjects(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void setConfig(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string key);
    void deleteConfig(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string key);
    void createEvent(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void analytics(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void listUsers(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void rotateSecret(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb);
};

}  // namespace web
