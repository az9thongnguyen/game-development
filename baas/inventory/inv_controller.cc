// =============================================================================
//  baas/inventory/inv_controller.cc  —  see inv_controller.h
// =============================================================================
#include "baas/inventory/inv_controller.h"

#include <exception>

#include <json/json.h>

#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/inventory/inv_service.h"

namespace web {
namespace {

drogon::HttpResponsePtr item_json(const inv::Item& it) {
    Json::Value j;
    j["item"] = it.item;
    j["qty"]  = static_cast<Json::Int64>(it.qty);
    return drogon::HttpResponse::newHttpJsonResponse(j);
}

// grant/consume share: parse {amount}, call fn, map the Result.
template <class Fn>
void adjust(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>& cb, Fn fn) {
    const auto body = req->getJsonObject();
    if (!body || !body->isMember("amount") || !(*body)["amount"].isIntegral()) {
        cb(make_error(400, "invalid_json", "expected {\"amount\": <positive integer>}"));
        return;
    }
    try {
        const auto r = fn((*body)["amount"].asInt64());
        if (r.error) cb(make_error(r.error->status, r.error->code, r.error->message));
        else         cb(item_json(*r.item));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "inventory update failed"));
    }
}

}  // namespace

void InventoryController::list(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);
    try {
        Json::Value arr(Json::arrayValue);
        for (const auto& it : inv::list(pid, uid)) {
            Json::Value j;
            j["item"] = it.item;
            j["qty"]  = static_cast<Json::Int64>(it.qty);
            arr.append(j);
        }
        Json::Value out;
        out["items"] = arr;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "inventory list failed"));
    }
}

void InventoryController::get(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                            std::string item) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);
    if (!inv::valid_item(item)) {
        cb(make_error(400, "invalid_item", "item must be 1-64 chars of [A-Za-z0-9_-]"));
        return;
    }
    try {
        cb(item_json(inv::get(pid, uid, item)));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "inventory get failed"));
    }
}

void InventoryController::grant(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                              std::string item) {
    const long        pid = req->attributes()->get<long>(kProjectId);
    const long        uid = req->attributes()->get<long>(kUserId);
    // Optional idempotency: a client retrying a timed-out grant sends the same
    // Idempotency-Key and is not double-credited. Capped so a stray header can't bloat a row.
    std::string idem = req->getHeader("idempotency-key");
    if (idem.size() > 64) idem.resize(64);
    adjust(req, cb, [&](long long amount) { return inv::grant(pid, uid, item, amount, idem); });
}

void InventoryController::consume(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                std::string item) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);
    adjust(req, cb, [&](long long amount) { return inv::consume(pid, uid, item, amount); });
}

}  // namespace web
