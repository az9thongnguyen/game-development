// =============================================================================
//  baas/test_runner/testrun_controller.h  —  HTTP edge for /v1/testruns
// =============================================================================
//  Project-scoped (api-key gate only): a run belongs to the game/operator, not a
//  player. create/list/get for submitters; claim/patch for the worker. The BaaS
//  coordinates; the worker (which links the engine) executes.
// =============================================================================
#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace web {

class TestRunController : public drogon::HttpController<TestRunController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TestRunController::create, "/v1/testruns",           drogon::Post, "web::ApiKeyFilter");
    ADD_METHOD_TO(TestRunController::list,   "/v1/testruns",           drogon::Get,  "web::ApiKeyFilter");
    ADD_METHOD_TO(TestRunController::get,    "/v1/testruns/{id}",      drogon::Get,  "web::ApiKeyFilter");
    ADD_METHOD_TO(TestRunController::claim,  "/v1/testruns/{id}/claim", drogon::Post, "web::ApiKeyFilter");
    ADD_METHOD_TO(TestRunController::patch,  "/v1/testruns/{id}",      drogon::Patch, "web::ApiKeyFilter");
    METHOD_LIST_END

    void create(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& cb);
    void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb, long long id);
    void claim(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& cb, long long id);
    void patch(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& cb, long long id);
};

}  // namespace web
