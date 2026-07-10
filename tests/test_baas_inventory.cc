// =============================================================================
//  tests/test_baas_inventory.cc  —  integration tests for inventory
// =============================================================================
//  grant/consume/get/list, the non-negative consume rule (409 insufficient),
//  item + amount validation, JWT requirement, and per-user + cross-tenant
//  isolation.
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

    const std::string db_path = "test_baas_inventory.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pkA = web::db::seed(db);
    db->execSqlSync("INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
                    std::string("Proj B"), std::string("pk_b"), std::string("unset"));
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
            return "Authorization: Bearer " + parse(r.body)["access_token"].asString();
        };
        const std::string keyA = "X-Api-Key: " + pkA;
        const std::string keyB = "X-Api-Key: " + pkB;
        const std::string a1   = tok(keyA, "a1@x.com");
        const std::string a2   = tok(keyA, "a2@x.com");
        const Resp        gB   = http("POST", base + "/v1/auth/guest", {keyB}, "{}");
        const std::string b    = "Authorization: Bearer " + parse(gB.body)["access_token"].asString();
        const std::string inv  = base + "/v1/inventory";

        // writes require a JWT
        CHECK(http("POST", inv + "/wood/grant", {keyA}, R"({"amount":5})").status == 401);

        // grant accumulates
        CHECK(parse(http("POST", inv + "/wood/grant", {keyA, a1}, R"({"amount":5})").body)["qty"].asInt() == 5);
        CHECK(parse(http("POST", inv + "/wood/grant", {keyA, a1}, R"({"amount":3})").body)["qty"].asInt() == 8);

        // get held + never-held (0, not 404)
        CHECK(parse(http("GET", inv + "/wood", {keyA, a1}).body)["qty"].asInt() == 8);
        Resp stone = http("GET", inv + "/stone", {keyA, a1});
        CHECK(stone.status == 200 && parse(stone.body)["qty"].asInt() == 0);

        // list
        CHECK(parse(http("GET", inv, {keyA, a1}).body)["items"].size() == 1);

        // consume ok, then insufficient (409) leaves qty unchanged
        CHECK(parse(http("POST", inv + "/wood/consume", {keyA, a1}, R"({"amount":3})").body)["qty"].asInt() == 5);
        CHECK(http("POST", inv + "/wood/consume", {keyA, a1}, R"({"amount":100})").status == 409);
        CHECK(parse(http("GET", inv + "/wood", {keyA, a1}).body)["qty"].asInt() == 5);   // unchanged
        CHECK(http("POST", inv + "/stone/consume", {keyA, a1}, R"({"amount":1})").status == 409);  // 0 < 1

        // validation: bad item, bad amounts
        CHECK(http("POST", inv + "/bad*item/grant", {keyA, a1}, R"({"amount":1})").status == 400);
        CHECK(http("POST", inv + "/wood/grant", {keyA, a1}, R"({"amount":0})").status == 400);
        CHECK(http("POST", inv + "/wood/grant", {keyA, a1}, R"({"amount":-5})").status == 400);
        CHECK(http("POST", inv + "/wood/grant", {keyA, a1}, R"({})").status == 400);

        // per-user isolation: a2 has no wood; cross-tenant B has none either
        CHECK(parse(http("GET", inv + "/wood", {keyA, a2}).body)["qty"].asInt() == 0);
        CHECK(parse(http("GET", inv, {keyA, a2}).body)["items"].size() == 0);
        CHECK(parse(http("GET", inv, {keyB, b}).body)["items"].size() == 0);

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_inventory: all tests passed\n");
    else                 std::printf("baas_inventory: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
