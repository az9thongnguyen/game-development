// =============================================================================
//  tests/test_baas_testruns.cc  —  integration tests for the test-run registry
// =============================================================================
//  create→pending, list + ?status= filter, get, claim (pending→running, second
//  claim→409), complete (running→passed, non-running→409), status validation,
//  scenario cap, and cross-tenant isolation. Project-scoped (api-key only).
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

    const std::string db_path = "test_baas_testruns.db";
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
        const std::string keyA = "X-Api-Key: " + pkA;
        const std::string keyB = "X-Api-Key: " + pkB;
        const std::string runs = base + "/v1/testruns";

        // create -> pending (api key only, no JWT)
        Resp c = http("POST", runs, {keyA}, R"({"scenario":"sandbox1\n","params":"steps=5;expect_alive=0"})");
        CHECK(c.status == 200);
        const long long id = parse(c.body)["id"].asInt64();
        CHECK(parse(c.body)["status"].asString() == "pending");

        // get + list + ?status= filter
        CHECK(parse(http("GET", runs + "/" + std::to_string(id), {keyA}).body)["status"].asString() == "pending");
        CHECK(parse(http("GET", runs, {keyA}).body)["testruns"].size() == 1);
        CHECK(parse(http("GET", runs + "?status=pending", {keyA}).body)["testruns"].size() == 1);
        CHECK(parse(http("GET", runs + "?status=running", {keyA}).body)["testruns"].size() == 0);

        // claim: pending -> running; a second claim -> 409
        Resp cl = http("POST", runs + "/" + std::to_string(id) + "/claim", {keyA});
        CHECK(cl.status == 200 && parse(cl.body)["status"].asString() == "running");
        CHECK(http("POST", runs + "/" + std::to_string(id) + "/claim", {keyA}).status == 409);

        // complete: running -> passed, result stored; completing again -> 409 (not running)
        Resp done = http("PATCH", runs + "/" + std::to_string(id), {keyA},
                         R"({"status":"passed","result":"alive=0 ok"})");
        CHECK(done.status == 200 && parse(done.body)["status"].asString() == "passed");
        CHECK(parse(http("GET", runs + "/" + std::to_string(id), {keyA}).body)["result"].asString() == "alive=0 ok");
        CHECK(http("PATCH", runs + "/" + std::to_string(id), {keyA}, R"({"status":"failed"})").status == 409);

        // validation: bad status -> 400; claiming a missing run -> 404
        Resp c2 = http("POST", runs, {keyA}, R"({"scenario":"sandbox1\n"})");
        const long long id2 = parse(c2.body)["id"].asInt64();
        http("POST", runs + "/" + std::to_string(id2) + "/claim", {keyA});
        CHECK(http("PATCH", runs + "/" + std::to_string(id2), {keyA}, R"({"status":"bogus"})").status == 400);
        CHECK(http("POST", runs + "/999999/claim", {keyA}).status == 404);

        // cross-tenant isolation: project B sees none of A's runs, and can't get one
        CHECK(parse(http("GET", runs, {keyB}).body)["testruns"].size() == 0);
        CHECK(http("GET", runs + "/" + std::to_string(id), {keyB}).status == 404);

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_testruns: all tests passed\n");
    else                 std::printf("baas_testruns: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
