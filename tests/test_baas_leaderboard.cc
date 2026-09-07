// =============================================================================
//  tests/test_baas_leaderboard.cc  —  integration tests for the leaderboard
// =============================================================================
//  Covers ranking + best-upsert, authenticated writes, the anti-spoof guarantee
//  (score belongs to the JWT's user, never a body field), and cross-tenant
//  isolation (project B never sees project A's scores).
// =============================================================================
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <drogon/drogon.h>
#include <sodium.h>

#include "baas/app_config.h"
#include "baas/app_setup.h"
#include "baas/db/db.h"
#include "baas/realtime/hub.h"
#include "tests/baas_test_util.h"

using baastest::http;
using baastest::parse;
using baastest::Resp;

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    web::set_config(web::AppConfig{"integration-test-secret", 3600});

    const std::string db_path = "test_baas_leaderboard.db";
    baastest::cleanup_db(db_path);

    auto db = web::db::make_db_client(baastest::db_url(db_path));
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pkA = web::db::seed(db);   // project A + colony_high

    // A second tenant (project B) with its own colony_high board.
    const long pidB = static_cast<long>(web::db::insert_id(db,
        "INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
        std::string("Proj B"), std::string("pk_b"), std::string("unset")));
    web::db::exec(db, "INSERT INTO leaderboards(project_id, key, name, sort) VALUES(?,?,?,?)",
                  pidB, std::string("colony_high"), std::string("B board"),
                  std::string("desc"));
    const std::string pkB = "pk_b";

    // A RATING board on project A. Same project, same sort, different `mode`: a
    // rating has to be able to go DOWN, and every board before chapter 139 kept the
    // better of the two values — which turns a ladder into a record of everybody's
    // best day.
    web::db::exec(db,
        "INSERT INTO leaderboards(project_id, key, name, sort, mode) VALUES(?,?,?,?,?)",
        static_cast<long>(web::db::exec(db, "SELECT id FROM projects WHERE public_key=?", pkA)[0]
                              ["id"].as<long>()),
        std::string("rating"), std::string("A ladder"), std::string("desc"),
        std::string("last"));

    const int         port = baastest::find_free_port();
    const std::string base = "http://127.0.0.1:" + std::to_string(port);

    drogon::app().setLogLevel(trantor::Logger::kError);
    web::register_routes();
    drogon::app().addListener("127.0.0.1", port);

    std::thread tester([&] {
        for (int i = 0; i < 200; ++i) {
            if (http("GET", base + "/healthz", {}).status == 200) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        const std::string keyA = "X-Api-Key: " + pkA;
        const std::string keyB = "X-Api-Key: " + pkB;
        const std::string board = base + "/v1/leaderboards/colony_high";

        // helper: register/guest → {token, id}
        auto reg = [&](const std::string& key, const std::string& email,
                       const std::string& name) {
            const Resp r = http("POST", base + "/v1/auth/register", {key},
                                R"({"email":")" + email + R"(","password":"secret1","display_name":")" +
                                    name + R"("})");
            return std::pair<std::string, long>{parse(r.body)["access_token"].asString(),
                                                parse(r.body)["user"]["user_id"].asInt64()};
        };
        auto bearer = [](const std::string& tok) { return "Authorization: Bearer " + tok; };

        const auto [tokA1, idA1] = reg(keyA, "a1@x.com", "A-One");
        const auto [tokA2, idA2] = reg(keyA, "a2@x.com", "A-Two");
        const Resp gB            = http("POST", base + "/v1/auth/guest", {keyB}, "{}");
        const std::string tokB   = parse(gB.body)["access_token"].asString();
        const long        idB    = parse(gB.body)["user"]["user_id"].asInt64();

        // --- writes require a JWT ---
        CHECK(http("POST", board + "/scores", {keyA}, R"({"value":10})").status == 401);

        // --- A1 submits 100, A2 submits 200 (desc → A2 first) ---
        Resp s = http("POST", board + "/scores", {keyA, bearer(tokA1)}, R"({"value":100})");
        CHECK(s.status == 200);
        CHECK(parse(s.body)["updated"].asBool() == true);
        CHECK(parse(s.body)["rank"].asInt() == 1);
        s = http("POST", board + "/scores", {keyA, bearer(tokA2)}, R"({"value":200})");
        CHECK(parse(s.body)["rank"].asInt() == 1);   // 200 beats 100

        Resp top = http("GET", board + "/top?limit=10", {keyA});
        CHECK(top.status == 200);
        CHECK(parse(top.body)["entries"].size() == 2);
        CHECK(parse(top.body)["entries"][0]["user_id"].asInt64() == idA2);   // 200 first
        CHECK(parse(top.body)["entries"][1]["user_id"].asInt64() == idA1);

        // --- best-keep: lower score is ignored, higher replaces ---
        s = http("POST", board + "/scores", {keyA, bearer(tokA1)}, R"({"value":50})");
        CHECK(parse(s.body)["updated"].asBool() == false);
        CHECK(parse(s.body)["value"].asInt64() == 100);       // unchanged
        s = http("POST", board + "/scores", {keyA, bearer(tokA1)}, R"({"value":150})");
        CHECK(parse(s.body)["updated"].asBool() == true);
        CHECK(parse(s.body)["value"].asInt64() == 150);
        CHECK(parse(s.body)["rank"].asInt() == 2);            // still behind A2's 200

        // --- me ---
        Resp me = http("GET", board + "/me", {keyA, bearer(tokA1)});
        CHECK(me.status == 200);
        CHECK(parse(me.body)["value"].asInt64() == 150 && parse(me.body)["rank"].asInt() == 2);

        // --- anti-spoof: a body user_id is ignored; the JWT's user is used ---
        s = http("POST", board + "/scores", {keyA, bearer(tokA1)},
                 R"({"value":999,"user_id":)" + std::to_string(idA2) + R"(})");
        CHECK(s.status == 200);
        CHECK(http("GET", board + "/me", {keyA, bearer(tokA1)}).body.find("999") != std::string::npos);  // A1 got 999
        CHECK(parse(http("GET", board + "/me", {keyA, bearer(tokA2)}).body)["value"].asInt64() == 200);   // A2 untouched

        // --- tenant isolation: B's score is invisible to A and vice-versa ---
        CHECK(http("POST", board + "/scores", {keyB, bearer(tokB)}, R"({"value":500})").status == 200);
        const auto topAj = parse(http("GET", board + "/top?limit=10", {keyA}).body);
        for (const auto& e : topAj["entries"])
            CHECK(e["user_id"].asInt64() != idB);             // A never sees B's user
        Resp topB = http("GET", board + "/top?limit=10", {keyB});
        CHECK(parse(topB.body)["entries"].size() == 1);
        CHECK(parse(topB.body)["entries"][0]["user_id"].asInt64() == idB);

        // --- a RATING board stores what it is handed, including a lower number ---
        {
            const std::string ladder = base + "/v1/leaderboards/rating";
            Resp r = http("POST", ladder + "/scores", {keyA, bearer(tokA1)}, R"({"value":1200})");
            CHECK(r.status == 200);
            CHECK(parse(r.body)["value"].asInt64() == 1200);

            r = http("POST", ladder + "/scores", {keyA, bearer(tokA1)}, R"({"value":1150})");
            CHECK(r.status == 200);
            CHECK(parse(r.body)["updated"].asBool() == true);
            CHECK(parse(r.body)["value"].asInt64() == 1150);        // it went DOWN
            CHECK(parse(http("GET", ladder + "/me", {keyA, bearer(tokA1)}).body)["value"]
                      .asInt64() == 1150);

            // ...and back up, so this is not a board that only ever overwrites in one
            // direction either.
            r = http("POST", ladder + "/scores", {keyA, bearer(tokA1)}, R"({"value":1400})");
            CHECK(parse(r.body)["value"].asInt64() == 1400);

            // The control, on the SAME server with the same user: the 'best' board
            // still refuses to lower. Without this the test above would pass on a
            // build where every board overwrites.
            Resp c = http("POST", board + "/scores", {keyA, bearer(tokA2)}, R"({"value":1})");
            CHECK(parse(c.body)["updated"].asBool() == false);
            CHECK(parse(c.body)["value"].asInt64() == 200);

            // ...and the ladder ranks by the rating, not by who submitted last.
            CHECK(http("POST", ladder + "/scores", {keyA, bearer(tokA2)},
                       R"({"value":1300})").status == 200);
            const auto lt = parse(http("GET", ladder + "/top?limit=10", {keyA}).body);
            CHECK(lt["entries"].size() == 2);
            CHECK(lt["entries"][0]["user_id"].asInt64() == idA1);   // 1400 > 1300
            CHECK(lt["entries"][1]["value"].asInt64() == 1300);
        }

        // --- a RATED MATCH: the server owns the arithmetic ---------------------
        // Everything above is a client PUTTING a number. A ladder cannot work that
        // way — the anti-spoof rule ("the score belongs to the JWT's user") means
        // nothing if the VALUE is still a body field — so this endpoint takes the
        // outcome and computes both ratings itself.
        {
            const std::string ladder = base + "/v1/leaderboards/rating";
            const std::string match  = ladder + "/match";
            const auto [tokC, idC] = reg(keyA, "c@x.com", "A-Three");
            const auto [tokD, idD] = reg(keyA, "d@x.com", "A-Four");

            // The server will only rate a match IT paired, so the pairings this test
            // reports have to exist. Registering them directly (rather than opening
            // four WebSockets) keeps this an HTTP test; `creature_pvp_live` is where
            // the real matchmaking path is driven.
            const long pidA = web::db::exec(db,
                "SELECT id FROM projects WHERE public_key=?", pkA)[0]["id"].as<long>();
            for (const char* room : {"match_1", "match_2", "match_3", "self", "m", "cross",
                                     "match_nope"})
                web::rt::RealtimeHub::instance().remember_match(pidA, room, idC, idD);

            const std::string body = R"({"opponent_id":)" + std::to_string(idD) +
                                     R"(,"result":"win","match":"match_1"})";
            Resp m = http("POST", match, {keyA, bearer(tokC)}, body);
            CHECK(m.status == 200);
            CHECK(parse(m.body)["applied"].asBool() == true);
            // Both fresh, so both started at 1200 and the win is worth K/2.
            CHECK(parse(m.body)["value"].asInt64() == 1216);
            CHECK(parse(m.body)["opponent_value"].asInt64() == 1184);
            CHECK(parse(m.body)["delta"].asInt() == 16);
            CHECK(parse(http("GET", ladder + "/me", {keyA, bearer(tokD)}).body)["value"]
                      .asInt64() == 1184);

            // THE one that matters: BOTH players report the same match, because both
            // played it. The second report must move nothing.
            const std::string echo = R"({"opponent_id":)" + std::to_string(idC) +
                                     R"(,"result":"loss","match":"match_1"})";
            Resp again = http("POST", match, {keyA, bearer(tokD)}, echo);
            CHECK(again.status == 200);
            CHECK(parse(again.body)["applied"].asBool() == false);
            CHECK(parse(again.body)["value"].asInt64() == 1184);       // still 1184
            // ...and NO delta. The stored one belongs to whoever reported first, and
            // handing it to the second reporter told the loser of the first real
            // match that they had gained sixteen points.
            CHECK(parse(again.body)["delta"].asInt() == 0);
            CHECK(parse(http("GET", ladder + "/me", {keyA, bearer(tokC)}).body)["value"]
                      .asInt64() == 1216);                             // and still 1216

            // A DIFFERENT match does move them — otherwise the check above would pass
            // on a server that ignored every report after the first one ever.
            const std::string second = R"({"opponent_id":)" + std::to_string(idD) +
                                       R"(,"result":"win","match":"match_2"})";
            Resp m2 = http("POST", match, {keyA, bearer(tokC)}, second);
            CHECK(parse(m2.body)["applied"].asBool() == true);
            CHECK(parse(m2.body)["value"].asInt64() > 1216);
            // Zero-sum, across two matches and two players.
            CHECK(parse(m2.body)["value"].asInt64() +
                      parse(m2.body)["opponent_value"].asInt64() == 2400);

            // A draw between two players who are now far apart moves them TOWARDS
            // each other, which is the direction that says the rating means something.
            const long before_c = parse(http("GET", ladder + "/me", {keyA, bearer(tokC)}).body)
                                      ["value"].asInt64();
            Resp dr = http("POST", match, {keyA, bearer(tokD)},
                           R"({"opponent_id":)" + std::to_string(idC) +
                               R"(,"result":"draw","match":"match_3"})");
            CHECK(dr.status == 200);
            CHECK(parse(dr.body)["delta"].asInt() > 0);                // the weaker one gains
            CHECK(parse(http("GET", ladder + "/me", {keyA, bearer(tokC)}).body)["value"]
                      .asInt64() < before_c);

            // ---- what it refuses ----
            CHECK(http("POST", match, {keyA}, body).status == 401);    // no JWT
            CHECK(http("POST", match, {keyA, bearer(tokC)},
                       R"({"opponent_id":)" + std::to_string(idC) +
                           R"(,"result":"win","match":"self"})").status == 400);   // yourself
            CHECK(http("POST", match, {keyA, bearer(tokC)},
                       R"({"opponent_id":)" + std::to_string(idD) +
                           R"(,"result":"win","match":""})").status == 400);       // blank match
            CHECK(http("POST", match, {keyA, bearer(tokC)},
                       R"({"opponent_id":)" + std::to_string(idD) +
                           R"(,"result":"maybe","match":"m"})").status == 400);    // bad outcome
            CHECK(http("POST", match, {keyA, bearer(tokC)},
                       R"({"result":"win","match":"m"})").status == 400);          // no opponent
            CHECK(http("POST", match, {keyA, bearer(tokC)},
                       R"({"opponent_id":999999,"result":"win","match":"m"})").status == 404);
            // ...and across tenants: B's user is not a player in A, whatever integer
            // the reporter writes down.
            CHECK(http("POST", match, {keyA, bearer(tokC)},
                       R"({"opponent_id":)" + std::to_string(idB) +
                           R"(,"result":"win","match":"cross"})").status == 404);
            CHECK(http("POST", base + "/v1/leaderboards/nope/match", {keyA, bearer(tokC)},
                       body).status == 404);
            // A match this server never made. Without this the endpoint would rate
            // any two players a client felt like naming, as often as it liked — the
            // same hole as letting the client pick the value, one level up.
            const long d_before = parse(http("GET", ladder + "/me", {keyA, bearer(tokD)}).body)
                                      ["value"].asInt64();
            CHECK(http("POST", match, {keyA, bearer(tokC)},
                       R"({"opponent_id":)" + std::to_string(idD) +
                           R"(,"result":"win","match":"never_happened"})").status == 403);
            // ...and a REAL match, reported by somebody who was not in it.
            CHECK(http("POST", match, {keyA, bearer(tokA1)},
                       R"({"opponent_id":)" + std::to_string(idD) +
                           R"(,"result":"win","match":"match_1"})").status == 403);
            // Neither attempt moved anything — a 403 that still wrote would be the
            // worst of both.
            CHECK(parse(http("GET", ladder + "/me", {keyA, bearer(tokD)}).body)["value"]
                      .asInt64() == d_before);
        }

        // --- validation: absurd value → 400; unknown board → 404 ---
        CHECK(http("POST", board + "/scores", {keyA, bearer(tokA1)},
                   R"({"value":2000000000000})").status == 400);
        CHECK(http("GET", base + "/v1/leaderboards/nope/top", {keyA}).status == 404);

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_leaderboard: all tests passed\n");
    else                 std::printf("baas_leaderboard: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
