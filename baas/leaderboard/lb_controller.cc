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
#include "baas/realtime/hub.h"

namespace web {

namespace {
constexpr long kMaxAbsValue = 1'000'000'000'000L;   // reject absurd scores (full anti-cheat = L4)
}

void LbController::top(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                      std::string key) {
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        const auto board = lb::find_board(pid, key);
        if (!board) { cb(make_error(404, "not_found", "leaderboard not found")); return; }

        int               limit = 10;
        const std::string ls    = req->getParameter("limit");
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
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "leaderboard query failed"));
    }
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
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);
    try {
        const auto board = lb::find_board(pid, key);
        if (!board) { cb(make_error(404, "not_found", "leaderboard not found")); return; }

        const auto e = lb::rank_of(*board, uid);
        if (!e) { cb(make_error(404, "no_score", "no score submitted yet")); return; }

        Json::Value out;
        out["rank"]  = e->rank;
        out["value"] = static_cast<Json::Int64>(e->value);
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "leaderboard query failed"));
    }
}

void LbController::match(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                        std::string key) {
    const long pid = req->attributes()->get<long>(kProjectId);
    const long uid = req->attributes()->get<long>(kUserId);

    const auto body = req->getJsonObject();
    if (!body || !(*body)["opponent_id"].isInt64() || !(*body)["result"].isString() ||
        !(*body)["match"].isString()) {
        cb(make_error(400, "invalid_json",
                      "expected {\"opponent_id\": N, \"result\": \"win|loss|draw\", "
                      "\"match\": \"...\"}"));
        return;
    }
    const long        opponent = static_cast<long>((*body)["opponent_id"].asInt64());
    const std::string outcome  = (*body)["result"].asString();
    const std::string match_id = (*body)["match"].asString();

    int result = 0;
    if (outcome == "win")       result = 1;
    else if (outcome == "loss") result = -1;
    else if (outcome != "draw") {
        cb(make_error(400, "invalid_result", "result must be win, loss or draw"));
        return;
    }
    // A blank match id would make every match idempotent against every other match,
    // which is a far worse failure than rejecting it: the second game anybody played
    // would silently do nothing.
    if (match_id.empty() || match_id.size() > 64) {
        cb(make_error(400, "invalid_match", "match must be 1-64 characters"));
        return;
    }
    // Playing yourself would be free rating in one direction and a lost row in the
    // other, because both writes address the same score.
    if (opponent == uid) {
        cb(make_error(400, "invalid_opponent", "a match needs two players"));
        return;
    }

    try {
        const auto board = lb::find_board(pid, key);
        if (!board) { cb(make_error(404, "not_found", "leaderboard not found")); return; }
        // The opponent has to be a real player IN THIS PROJECT. Without this, a
        // reporter could name any integer and mint a rating row for it — or reach
        // across tenants, which nothing else here allows.
        if (!lb::user_in_project(pid, opponent)) {
            cb(make_error(404, "no_opponent", "no such player in this project"));
            return;
        }
        // ...and the two of them have to have actually been MATCHED, in this room,
        // by this server. Letting a client pick the value would make the board
        // decoration; letting it pick the opponent and the occasion is the same hole
        // one level up — "I beat the top player forty times" needs no forged score.
        //
        // The cost of this line is that `/match` only works for a game that uses the
        // hub's matchmaking, and only until the server restarts. Both are true of the
        // hub itself, which is where the knowledge lives.
        if (!rt::RealtimeHub::instance().was_matched(pid, match_id, uid, opponent)) {
            cb(make_error(403, "no_such_match",
                          "this server did not pair those two players in that match"));
            return;
        }

        const lb::MatchResult m = lb::apply_match(pid, *board, uid, opponent, result, match_id);
        Json::Value out;
        out["value"]          = static_cast<Json::Int64>(m.value);
        out["opponent_value"] = static_cast<Json::Int64>(m.opponent_value);
        out["rank"]           = m.rank;
        out["delta"]          = m.delta;
        out["applied"]        = m.applied;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "match update failed"));
    }
}

}  // namespace web
