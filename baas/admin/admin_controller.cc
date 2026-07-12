// =============================================================================
//  baas/admin/admin_controller.cc  —  see admin_controller.h
// =============================================================================
#include "baas/admin/admin_controller.h"

#include <cctype>
#include <exception>

#include <json/json.h>

#include "baas/admin/admin_service.h"
#include "baas/analytics/analytics_service.h"
#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/live_events/events_service.h"
#include "baas/remote_config/config_service.h"

namespace web {
namespace {

bool valid_key(const std::string& k) {   // config/event key: 1-64 of [A-Za-z0-9_.-]
    if (k.empty() || k.size() > 64) return false;
    for (char c : k)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
            return false;
    return true;
}

}  // namespace

void AdminController::createProject(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const auto body = req->getJsonObject();
    if (!body || !(*body)["name"].isString() || (*body)["name"].asString().empty()) {
        cb(make_error(400, "invalid_json", "expected {\"name\": \"...\"}"));
        return;
    }
    try {
        const auto p = admin::create_project((*body)["name"].asString());
        Json::Value out;
        out["id"]         = static_cast<Json::Int64>(p.id);
        out["name"]       = p.name;
        out["public_key"] = p.public_key;
        out["secret_key"] = p.secret_key;   // shown once
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "project creation failed"));
    }
}

void AdminController::listProjects(const drogon::HttpRequestPtr&,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    try {
        Json::Value arr(Json::arrayValue);
        for (const auto& p : admin::list_projects()) {
            Json::Value j;
            j["id"]         = static_cast<Json::Int64>(p.id);
            j["name"]       = p.name;
            j["public_key"] = p.public_key;
            arr.append(j);
        }
        Json::Value out;
        out["projects"] = arr;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "project list failed"));
    }
}

void AdminController::setConfig(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                               std::string key) {
    const long pid  = req->attributes()->get<long>(kProjectId);
    const auto body = req->getJsonObject();
    if (!valid_key(key)) { cb(make_error(400, "invalid_key", "bad config key")); return; }
    if (!body || !(*body)["value"].isString()) {
        cb(make_error(400, "invalid_json", "expected {\"value\": \"...\"}"));
        return;
    }
    try {
        // Audited LiveOps change: records old→new and returns the prior value so the
        // operator (or the dashboard) can revert without a client redeployment.
        const auto prev = cfg::set_audited(pid, key, (*body)["value"].asString(), "admin");
        Json::Value out;
        out["key"]      = key;
        out["value"]    = (*body)["value"].asString();
        if (prev) out["previous"] = *prev;   // present unless the key was newly created
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "config set failed"));
    }
}

void AdminController::deleteConfig(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                  std::string key) {
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        const auto prev = cfg::remove_audited(pid, key, "admin");
        if (!prev) { cb(make_error(404, "not_found", "no such key")); return; }
        Json::Value out;
        out["deleted"]  = true;
        out["previous"] = *prev;   // returned so the delete is revertible via a set
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "config delete failed"));
    }
}

void AdminController::createEvent(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid  = req->attributes()->get<long>(kProjectId);
    const auto body = req->getJsonObject();
    if (!body || !(*body)["key"].isString() || !(*body)["name"].isString() ||
        !(*body)["starts_at"].isString() || !(*body)["ends_at"].isString()) {
        cb(make_error(400, "invalid_json",
                      "expected {key,name,starts_at,ends_at,payload?}"));
        return;
    }
    const std::string key = (*body)["key"].asString();
    if (!valid_key(key)) { cb(make_error(400, "invalid_key", "bad event key")); return; }
    const std::string payload = (*body)["payload"].isString() ? (*body)["payload"].asString() : "{}";
    try {
        live::create(pid, key, (*body)["name"].asString(), (*body)["starts_at"].asString(),
                     (*body)["ends_at"].asString(), payload);
        Json::Value out;
        out["key"] = key;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "event create failed"));
    }
}

void AdminController::analytics(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        Json::Value arr(Json::arrayValue);
        for (const auto& c : web::analytics::summary(pid)) {
            Json::Value j;
            j["name"]  = c.name;
            j["count"] = static_cast<Json::Int64>(c.count);
            arr.append(j);
        }
        Json::Value out;
        out["counts"] = arr;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "analytics summary failed"));
    }
}

void AdminController::listUsers(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        Json::Value arr(Json::arrayValue);
        for (const auto& u : admin::list_users(pid)) {
            Json::Value j;
            j["id"]           = static_cast<Json::Int64>(u.id);
            j["email"]        = u.email;
            j["display_name"] = u.display_name;
            j["is_guest"]     = u.is_guest;
            arr.append(j);
        }
        Json::Value out;
        out["users"] = arr;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "user list failed"));
    }
}

void AdminController::rotateSecret(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    // Reached only after SecretKeyFilter proved the caller holds the CURRENT secret.
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        const std::string secret = admin::rotate_secret(pid);
        Json::Value out;
        out["secret_key"] = secret;   // shown once; the previous secret stops working now
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "secret rotation failed"));
    }
}

}  // namespace web
