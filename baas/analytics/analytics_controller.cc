// =============================================================================
//  baas/analytics/analytics_controller.cc  —  see analytics_controller.h
// =============================================================================
#include "baas/analytics/analytics_controller.h"

#include <exception>
#include <string>

#include <json/json.h>

#include "baas/analytics/analytics_service.h"
#include "baas/app_config.h"
#include "baas/auth/jwt.h"
#include "baas/common/context_keys.h"
#include "baas/common/errors.h"

namespace web {
namespace {
constexpr std::size_t kMaxProps = 4 * 1024;   // opaque JSON props cap
}

void AnalyticsController::track(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid  = req->attributes()->get<long>(kProjectId);
    const auto body = req->getJsonObject();
    if (!body || !body->isMember("name") || !(*body)["name"].isString()) {
        cb(make_error(400, "invalid_json", "expected {\"name\": \"...\", \"props\": {...}?}"));
        return;
    }
    const std::string name = (*body)["name"].asString();
    if (!analytics::valid_name(name)) {
        cb(make_error(400, "invalid_name", "name must be 1-64 chars of [A-Za-z0-9_.-]"));
        return;
    }
    // props: accept an object (serialized) or a string; store opaque JSON, capped.
    std::string props = "{}";
    if (body->isMember("props")) {
        const auto& p = (*body)["props"];
        props = p.isString() ? p.asString() : Json::writeString(Json::StreamWriterBuilder(), p);
    }
    if (props.size() > kMaxProps) {
        cb(make_error(413, "too_large", "props exceeds 4 KiB"));
        return;
    }

    // Best-effort attribution: use the Bearer user if present and matching this project.
    long              uid  = 0;
    const std::string auth = req->getHeader("authorization");
    if (auth.rfind("Bearer ", 0) == 0) {
        const auto claims = jwt::verify(auth.substr(7), config().jwt_secret);
        if (claims && claims->pid == pid) uid = claims->sub;
    }

    try {
        analytics::record(pid, uid, name, props);
        Json::Value out;
        out["ok"] = true;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "event ingest failed"));
    }
}

}  // namespace web
