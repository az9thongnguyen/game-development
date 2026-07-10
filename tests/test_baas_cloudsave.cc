// =============================================================================
//  tests/test_baas_cloudsave.cc  —  integration tests for cloud save
// =============================================================================
//  put/get round-trip, version bump, list metadata, delete, optimistic
//  concurrency (If-Match), payload cap, slot validation, JWT requirement, and
//  per-user + cross-tenant isolation.
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

    const std::string db_path = "test_baas_cloudsave.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pkA = web::db::seed(db);

    const auto insB = db->execSqlSync(
        "INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
        std::string("Proj B"), std::string("pk_b"), std::string("unset"));
    (void)insB;
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

        auto tok = [&](const std::string& key, const std::string& email) {
            const Resp r = http("POST", base + "/v1/auth/register", {key},
                                R"({"email":")" + email + R"(","password":"secret1","display_name":"U"})");
            return parse(r.body)["access_token"].asString();
        };
        const std::string keyA = "X-Api-Key: " + pkA;
        const std::string keyB = "X-Api-Key: " + pkB;
        const std::string a1   = "Authorization: Bearer " + tok(keyA, "a1@x.com");
        const std::string a2   = "Authorization: Bearer " + tok(keyA, "a2@x.com");
        const Resp        gB   = http("POST", base + "/v1/auth/guest", {keyB}, "{}");
        const std::string b    = "Authorization: Bearer " + parse(gB.body)["access_token"].asString();
        const std::string saves = base + "/v1/saves";

        // writes require a JWT
        CHECK(http("PUT", saves + "/colony", {keyA}, R"({"data":"x"})").status == 401);

        // put → get round-trip
        Resp p = http("PUT", saves + "/colony", {keyA, a1}, R"({"data":"{\"hp\":10}"})");
        CHECK(p.status == 200);
        CHECK(parse(p.body)["version"].asInt() == 1);
        Resp g = http("GET", saves + "/colony", {keyA, a1});
        CHECK(g.status == 200);
        CHECK(parse(g.body)["data"].asString() == R"({"hp":10})");
        CHECK(parse(g.body)["version"].asInt() == 1);

        // update bumps the version
        p = http("PUT", saves + "/colony", {keyA, a1}, R"({"data":"{\"hp\":20}"})");
        CHECK(parse(p.body)["version"].asInt() == 2);
        CHECK(parse(http("GET", saves + "/colony", {keyA, a1}).body)["data"].asString() == R"({"hp":20})");

        // list metadata (data not included)
        http("PUT", saves + "/settings", {keyA, a1}, R"({"data":"vol=5"})");
        Resp l = http("GET", saves, {keyA, a1});
        CHECK(parse(l.body)["saves"].size() == 2);

        // optimistic concurrency: wrong If-Match → 409, correct → 200
        CHECK(http("PUT", saves + "/colony", {keyA, a1, "If-Match: 99"}, R"({"data":"z"})").status == 409);
        CHECK(http("PUT", saves + "/colony", {keyA, a1, "If-Match: 2"}, R"({"data":"z"})").status == 200);

        // per-user isolation: a2 cannot see a1's save, and has none of their own
        CHECK(http("GET", saves + "/colony", {keyA, a2}).status == 404);
        CHECK(parse(http("GET", saves, {keyA, a2}).body)["saves"].size() == 0);

        // cross-tenant isolation: project B user sees nothing of A's
        CHECK(parse(http("GET", saves, {keyB, b}).body)["saves"].size() == 0);
        CHECK(http("GET", saves + "/colony", {keyB, b}).status == 404);

        // validation: bad slot → 400; oversized payload → 413
        CHECK(http("PUT", saves + "/bad*slot", {keyA, a1}, R"({"data":"x"})").status == 400);
        const std::string big(300 * 1024, 'x');
        CHECK(http("PUT", saves + "/huge", {keyA, a1}, R"({"data":")" + big + R"("})").status == 413);

        // delete → gone
        CHECK(http("DELETE", saves + "/settings", {keyA, a1}).status == 200);
        CHECK(http("GET", saves + "/settings", {keyA, a1}).status == 404);
        CHECK(http("DELETE", saves + "/settings", {keyA, a1}).status == 404);

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_cloudsave: all tests passed\n");
    else                 std::printf("baas_cloudsave: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
