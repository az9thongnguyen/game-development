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

    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pkA = web::db::seed(db);   // project A + colony_high

    // A second tenant (project B) with its own colony_high board.
    const auto insB = db->execSqlSync(
        "INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
        std::string("Proj B"), std::string("pk_b"), std::string("unset"));
    db->execSqlSync("INSERT INTO leaderboards(project_id, key, name, sort) VALUES(?,?,?,?)",
                    static_cast<long>(insB.insertId()), std::string("colony_high"),
                    std::string("B board"), std::string("desc"));
    const std::string pkB = "pk_b";

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
