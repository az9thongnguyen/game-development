// =============================================================================
//  baas/cloud_save/save_controller.cc  —  see save_controller.h
// =============================================================================
#include "baas/cloud_save/save_controller.h"

#include <cstddef>
#include <cstdlib>
#include <exception>

#include <json/json.h>

#include "baas/cloud_save/save_service.h"
#include "baas/common/context_keys.h"
#include "baas/common/errors.h"

namespace web {
namespace {
constexpr std::size_t kMaxSaveBytes = 256 * 1024;   // per-payload cap (storage-DoS guard)
}

void SaveController::put(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                        std::string slot) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);
    if (!save::valid_slot(slot)) {
        cb(make_error(400, "invalid_slot", "slot must be 1-64 chars of [A-Za-z0-9_-]"));
        return;
    }
    const auto body = req->getJsonObject();
    if (!body || !body->isMember("data") || !(*body)["data"].isString()) {
        cb(make_error(400, "invalid_json", "expected {\"data\": \"...\"}"));
        return;
    }
    const std::string data = (*body)["data"].asString();
    if (data.size() > kMaxSaveBytes) {
        cb(make_error(413, "too_large", "save payload exceeds 256 KiB"));
        return;
    }
    long long         if_match = 0;   // 0 = no check
    const std::string im       = req->getHeader("if-match");
    if (!im.empty()) if_match = std::atoll(im.c_str());

    try {
        const auto r = save::put(pid, uid, slot, data, if_match);
        if (r.error) { cb(make_error(r.error->status, r.error->code, r.error->message)); return; }
        Json::Value out;
        out["slot"]    = r.meta->slot;
        out["version"] = static_cast<Json::Int64>(r.meta->version);
        out["size"]    = static_cast<Json::Int64>(r.meta->size);
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "save failed"));
    }
}

void SaveController::get(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                        std::string slot) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);
    if (!save::valid_slot(slot)) {
        cb(make_error(400, "invalid_slot", "slot must be 1-64 chars of [A-Za-z0-9_-]"));
        return;
    }
    try {
        const auto rec = save::get(pid, uid, slot);
        if (!rec) { cb(make_error(404, "not_found", "no such save")); return; }
        Json::Value out;
        out["slot"]       = rec->slot;
        out["version"]    = static_cast<Json::Int64>(rec->version);
        out["data"]       = rec->data;
        out["updated_at"] = rec->updated_at;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "load failed"));
    }
}

void SaveController::list(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);
    try {
        Json::Value arr(Json::arrayValue);
        for (const auto& m : save::list(pid, uid)) {
            Json::Value j;
            j["slot"]       = m.slot;
            j["version"]    = static_cast<Json::Int64>(m.version);
            j["size"]       = static_cast<Json::Int64>(m.size);
            j["updated_at"] = m.updated_at;
            arr.append(j);
        }
        Json::Value out;
        out["saves"] = arr;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "list failed"));
    }
}

void SaveController::remove(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                           std::string slot) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);
    if (!save::valid_slot(slot)) {
        cb(make_error(400, "invalid_slot", "slot must be 1-64 chars of [A-Za-z0-9_-]"));
        return;
    }
    try {
        if (!save::remove(pid, uid, slot)) {
            cb(make_error(404, "not_found", "no such save"));
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
