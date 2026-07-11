// =============================================================================
//  baas/test_runner/testrun_controller.cc  —  see testrun_controller.h
// =============================================================================
#include "baas/test_runner/testrun_controller.h"

#include <cstddef>
#include <exception>

#include <json/json.h>

#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/test_runner/testrun_service.h"

namespace web {
namespace {
constexpr std::size_t kMaxScenarioBytes = 256 * 1024;

Json::Value to_json(const testrun::Record& r) {
    Json::Value j;
    j["id"]         = static_cast<Json::Int64>(r.id);
    j["scenario"]   = r.scenario;
    j["params"]     = r.params;
    j["status"]     = r.status;
    j["result"]     = r.result;
    j["created_at"] = r.created_at;
    j["updated_at"] = r.updated_at;
    return j;
}
}  // namespace

void TestRunController::create(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid  = req->attributes()->get<long>(kProjectId);
    const auto body = req->getJsonObject();
    if (!body || !body->isMember("scenario") || !(*body)["scenario"].isString()) {
        cb(make_error(400, "invalid_json", "expected {\"scenario\": \"...\"}"));
        return;
    }
    const std::string scenario = (*body)["scenario"].asString();
    const std::string params   = body->isMember("params") && (*body)["params"].isString()
                                     ? (*body)["params"].asString() : std::string();
    if (scenario.size() > kMaxScenarioBytes) {
        cb(make_error(413, "too_large", "scenario exceeds 256 KiB"));
        return;
    }
    try {
        const long long id = testrun::create(pid, scenario, params);
        Json::Value out;
        out["id"]     = static_cast<Json::Int64>(id);
        out["status"] = "pending";
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "create failed"));
    }
}

void TestRunController::list(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long        pid    = req->attributes()->get<long>(kProjectId);
    const std::string status = req->getParameter("status");   // optional ?status= filter
    try {
        Json::Value arr(Json::arrayValue);
        for (const auto& r : testrun::list(pid, status)) arr.append(to_json(r));
        Json::Value out;
        out["testruns"] = arr;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "list failed"));
    }
}

void TestRunController::get(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& cb, long long id) {
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        const auto rec = testrun::get(pid, id);
        if (!rec) { cb(make_error(404, "not_found", "no such run")); return; }
        cb(drogon::HttpResponse::newHttpJsonResponse(to_json(*rec)));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "get failed"));
    }
}

void TestRunController::claim(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb, long long id) {
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        if (!testrun::get(pid, id)) { cb(make_error(404, "not_found", "no such run")); return; }
        if (!testrun::claim(pid, id)) {
            cb(make_error(409, "not_claimable", "run is not pending"));
            return;
        }
        const auto rec = testrun::get(pid, id);
        cb(drogon::HttpResponse::newHttpJsonResponse(to_json(*rec)));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "claim failed"));
    }
}

void TestRunController::patch(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb, long long id) {
    const long pid  = req->attributes()->get<long>(kProjectId);
    const auto body = req->getJsonObject();
    if (!body || !body->isMember("status") || !(*body)["status"].isString()) {
        cb(make_error(400, "invalid_json", "expected {\"status\": \"passed|failed|error\"}"));
        return;
    }
    const std::string status = (*body)["status"].asString();
    const std::string result = body->isMember("result") && (*body)["result"].isString()
                                   ? (*body)["result"].asString() : std::string();
    if (!testrun::valid_status(status)) {
        cb(make_error(400, "invalid_status", "status must be passed|failed|error"));
        return;
    }
    try {
        if (!testrun::get(pid, id)) { cb(make_error(404, "not_found", "no such run")); return; }
        if (!testrun::complete(pid, id, status, result)) {
            cb(make_error(409, "not_running", "run is not in the running state"));
            return;
        }
        const auto rec = testrun::get(pid, id);
        cb(drogon::HttpResponse::newHttpJsonResponse(to_json(*rec)));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "patch failed"));
    }
}

}  // namespace web
