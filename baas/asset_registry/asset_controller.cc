// =============================================================================
//  baas/asset_registry/asset_controller.cc  —  see asset_controller.h
// =============================================================================
#include "baas/asset_registry/asset_controller.h"

#include <cstddef>
#include <cstdlib>
#include <exception>

#include <json/json.h>

#include "baas/asset_registry/asset_service.h"
#include "baas/common/context_keys.h"
#include "baas/common/errors.h"

namespace web {
namespace {
constexpr std::size_t kMaxAssetBytes = 1024 * 1024;   // 1 MiB per-payload cap (storage-DoS guard)
}

void AssetController::put(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                         std::string name) {
    const long pid = req->attributes()->get<long>(kProjectId);
    if (!asset::valid_name(name)) {
        cb(make_error(400, "invalid_name", "name must be 1-128 chars of [A-Za-z0-9._-]"));
        return;
    }
    const auto body = req->getJsonObject();
    if (!body || !body->isMember("data") || !(*body)["data"].isString()) {
        cb(make_error(400, "invalid_json", "expected {\"data\": \"...\"}"));
        return;
    }
    const std::string data = (*body)["data"].asString();
    const std::string kind = body->isMember("kind") && (*body)["kind"].isString()
                                 ? (*body)["kind"].asString() : std::string();
    if (!asset::valid_kind(kind)) {
        cb(make_error(400, "invalid_kind", "kind must be 0-32 chars of [A-Za-z0-9_-]"));
        return;
    }
    if (data.size() > kMaxAssetBytes) {
        cb(make_error(413, "too_large", "asset payload exceeds 1 MiB"));
        return;
    }
    long long         if_match = 0;   // 0 = no check
    const std::string im       = req->getHeader("if-match");
    if (!im.empty()) if_match = std::atoll(im.c_str());

    try {
        const auto r = asset::put(pid, name, kind, data, if_match);
        if (r.error) { cb(make_error(r.error->status, r.error->code, r.error->message)); return; }
        Json::Value out;
        out["name"]    = r.meta->name;
        out["kind"]    = r.meta->kind;
        out["version"] = static_cast<Json::Int64>(r.meta->version);
        out["size"]    = static_cast<Json::Int64>(r.meta->size);
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "put failed"));
    }
}

void AssetController::get(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                         std::string name) {
    const long pid = req->attributes()->get<long>(kProjectId);
    if (!asset::valid_name(name)) {
        cb(make_error(400, "invalid_name", "name must be 1-128 chars of [A-Za-z0-9._-]"));
        return;
    }
    try {
        const auto rec = asset::get(pid, name);
        if (!rec) { cb(make_error(404, "not_found", "no such asset")); return; }
        Json::Value out;
        out["name"]       = rec->name;
        out["kind"]       = rec->kind;
        out["version"]    = static_cast<Json::Int64>(rec->version);
        out["data"]       = rec->data;
        out["updated_at"] = rec->updated_at;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "get failed"));
    }
}

void AssetController::list(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long        pid  = req->attributes()->get<long>(kProjectId);
    const std::string kind = req->getParameter("kind");   // optional ?kind= filter ("" = all)
    try {
        Json::Value arr(Json::arrayValue);
        for (const auto& m : asset::list(pid, kind)) {
            Json::Value j;
            j["name"]       = m.name;
            j["kind"]       = m.kind;
            j["version"]    = static_cast<Json::Int64>(m.version);
            j["size"]       = static_cast<Json::Int64>(m.size);
            j["updated_at"] = m.updated_at;
            arr.append(j);
        }
        Json::Value out;
        out["assets"] = arr;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "list failed"));
    }
}

void AssetController::remove(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                            std::string name) {
    const long pid = req->attributes()->get<long>(kProjectId);
    if (!asset::valid_name(name)) {
        cb(make_error(400, "invalid_name", "name must be 1-128 chars of [A-Za-z0-9._-]"));
        return;
    }
    try {
        if (!asset::remove(pid, name)) {
            cb(make_error(404, "not_found", "no such asset"));
            return;
        }
        Json::Value out;
        out["deleted"] = true;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "delete failed"));
    }
}

}  // namespace web
