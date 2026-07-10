// =============================================================================
//  baas/replays/replay_controller.cc  —  see replay_controller.h
// =============================================================================
#include "baas/replays/replay_controller.h"

#include <cstddef>
#include <cstdlib>
#include <exception>

#include <json/json.h>

#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/replays/replay_service.h"

namespace web {
namespace {
constexpr std::size_t kMaxReplayBytes = 512 * 1024;   // per-replay cap (storage-DoS guard)
}

void ReplayController::create(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid  = req->attributes()->get<long>(kProjectId);
    const long uid  = req->attributes()->get<long>(kUserId);
    const auto body = req->getJsonObject();
    if (!body || !(*body)["name"].isString() || !(*body)["data"].isString()) {
        cb(make_error(400, "invalid_json", "expected {\"name\": \"...\", \"data\": \"...\"}"));
        return;
    }
    const std::string name = (*body)["name"].asString();
    const std::string data = (*body)["data"].asString();
    if (!replay::valid_name(name)) {
        cb(make_error(400, "invalid_name", "name must be 1-64 printable characters"));
        return;
    }
    if (data.size() > kMaxReplayBytes) {
        cb(make_error(413, "too_large", "replay payload exceeds 512 KiB"));
        return;
    }
    try {
        const long long id = replay::create(pid, uid, name, data);
        Json::Value out;
        out["id"]   = static_cast<Json::Int64>(id);
        out["name"] = name;
        out["size"] = static_cast<Json::Int64>(data.size());
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "replay create failed"));
    }
}

void ReplayController::list(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);
    try {
        Json::Value arr(Json::arrayValue);
        for (const auto& m : replay::list(pid, uid)) {
            Json::Value j;
            j["id"]         = static_cast<Json::Int64>(m.id);
            j["name"]       = m.name;
            j["size"]       = static_cast<Json::Int64>(m.size);
            j["created_at"] = m.created_at;
            arr.append(j);
        }
        Json::Value out;
        out["replays"] = arr;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "replay list failed"));
    }
}

void ReplayController::get(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                          std::string id) {
    const long      pid = req->attributes()->get<long>(kProjectId);
    const long      uid = req->attributes()->get<long>(kUserId);
    const long long rid = std::strtoll(id.c_str(), nullptr, 10);
    try {
        const auto rec = replay::get(pid, uid, rid);
        if (!rec) { cb(make_error(404, "not_found", "no such replay")); return; }
        Json::Value out;
        out["id"]         = static_cast<Json::Int64>(rec->id);
        out["name"]       = rec->name;
        out["data"]       = rec->data;
        out["created_at"] = rec->created_at;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "replay load failed"));
    }
}

void ReplayController::remove(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                             std::string id) {
    const long      pid = req->attributes()->get<long>(kProjectId);
    const long      uid = req->attributes()->get<long>(kUserId);
    const long long rid = std::strtoll(id.c_str(), nullptr, 10);
    try {
        if (!replay::remove(pid, uid, rid)) {
            cb(make_error(404, "not_found", "no such replay"));
            return;
        }
        Json::Value out;
        out["deleted"] = true;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "replay delete failed"));
    }
}

}  // namespace web
