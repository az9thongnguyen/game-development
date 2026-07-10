// =============================================================================
//  baas/leaderboard/lb_controller.cc  —  see lb_controller.h
// =============================================================================
#include "baas/leaderboard/lb_controller.h"

#include <cstdlib>
#include <exception>

#include <json/json.h>

#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/leaderboard/lb_service.h"

namespace web {

namespace {
constexpr long kMaxAbsValue = 1'000'000'000'000L;   // reject absurd scores (full anti-cheat = L4)
}

void LbController::top(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                      std::string key) {
    const long pid   = req->attributes()->get<long>(kProjectId);
    const auto board = lb::find_board(pid, key);
    if (!board) { cb(make_error(404, "not_found", "leaderboard not found")); return; }

    int              limit = 10;
    const std::string ls   = req->getParameter("limit");
    if (!ls.empty()) limit = std::atoi(ls.c_str());
    if (limit < 1)   limit = 1;
    if (limit > 100) limit = 100;   // clamp

    Json::Value entries(Json::arrayValue);
    for (const auto& e : lb::top(*board, limit)) {
        Json::Value j;
        j["rank"]         = e.rank;
        j["user_id"]      = static_cast<Json::Int64>(e.user_id);
        j["display_name"] = e.display_name;
        j["value"]        = static_cast<Json::Int64>(e.value);
        entries.append(j);
    }
    Json::Value out;
    out["entries"] = entries;
    cb(drogon::HttpResponse::newHttpJsonResponse(out));
}

void LbController::submit(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                         std::string key) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);   // from the JWT, NOT the body
    const auto board = lb::find_board(pid, key);
    if (!board) { cb(make_error(404, "not_found", "leaderboard not found")); return; }

    const auto body = req->getJsonObject();
    if (!body || !body->isMember("value")) {
        cb(make_error(400, "invalid_json", "expected {\"value\": <number>}"));
        return;
    }
    const long value = (*body)["value"].asInt64();
    if (value < -kMaxAbsValue || value > kMaxAbsValue) {
        cb(make_error(400, "out_of_range", "value out of allowed range"));
        return;
    }
    try {
        const auto res = lb::submit(*board, uid, value);
        Json::Value out;
        out["rank"]    = res.rank;
        out["value"]   = static_cast<Json::Int64>(res.value);
        out["updated"] = res.updated;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "score submission failed"));
    }
}

void LbController::me(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                     std::string key) {
    const long pid   = req->attributes()->get<long>(kProjectId);
    const long uid   = req->attributes()->get<long>(kUserId);
    const auto board = lb::find_board(pid, key);
    if (!board) { cb(make_error(404, "not_found", "leaderboard not found")); return; }

    const auto e = lb::rank_of(*board, uid);
    if (!e) { cb(make_error(404, "no_score", "no score submitted yet")); return; }

    Json::Value out;
    out["rank"]  = e->rank;
    out["value"] = static_cast<Json::Int64>(e->value);
    cb(drogon::HttpResponse::newHttpJsonResponse(out));
}

}  // namespace web
