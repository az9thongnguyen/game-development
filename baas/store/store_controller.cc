// =============================================================================
//  baas/store/store_controller.cc  —  see store_controller.h
// =============================================================================
#include "baas/store/store_controller.h"

#include <exception>

#include <json/json.h>

#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/store/store_service.h"

namespace web {

void StoreController::catalog(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        Json::Value arr(Json::arrayValue);
        for (const auto& o : store::list(pid)) {
            Json::Value j;
            j["sku"]      = o.sku;
            j["currency"] = o.currency;
            j["cost"]     = static_cast<Json::Int64>(o.cost);
            j["item"]     = o.item;
            j["amount"]   = static_cast<Json::Int64>(o.amount);
            arr.append(j);
        }
        Json::Value out;
        out["offers"] = arr;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "catalog list failed"));
    }
}

void StoreController::buy(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                        std::string sku) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);
    if (!store::valid_sku(sku)) {
        cb(make_error(400, "invalid_sku", "sku must be 1-64 chars of [A-Za-z0-9_.-]"));
        return;
    }
    // Optional idempotency: a retried buy carries the same Idempotency-Key and is not
    // charged twice (see inv::purchase). Capped so a stray header can't bloat a row.
    std::string idem = req->getHeader("idempotency-key");
    if (idem.size() > 64) idem.resize(64);
    try {
        const auto r = store::buy(pid, uid, sku, idem);
        if (r.error) {
            cb(make_error(r.error->status, r.error->code, r.error->message));
        } else {
            Json::Value j;
            j["item"] = r.item->item;
            j["qty"]  = static_cast<Json::Int64>(r.item->qty);
            cb(drogon::HttpResponse::newHttpJsonResponse(j));
        }
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "purchase failed"));
    }
}

}  // namespace web
