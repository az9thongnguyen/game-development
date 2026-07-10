// =============================================================================
//  baas/remote_config/config_controller.cc  —  see config_controller.h
// =============================================================================
#include "baas/remote_config/config_controller.h"

#include <exception>

#include <json/json.h>

#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/remote_config/config_service.h"

namespace web {

void ConfigController::all(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        Json::Value config(Json::objectValue);
        for (const auto& kv : cfg::all(pid)) config[kv.key] = kv.value;
        Json::Value out;
        out["config"] = config;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "config read failed"));
    }
}

void ConfigController::get(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                          std::string key) {
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        const auto v = cfg::get(pid, key);
        if (!v) { cb(make_error(404, "not_found", "no such config key")); return; }
        Json::Value out;
        out["key"]   = key;
        out["value"] = *v;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "config read failed"));
    }
}

}  // namespace web
