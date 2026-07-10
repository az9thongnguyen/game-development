// =============================================================================
//  tests/test_baas_config.cc  —  integration tests for remote config (read)
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
    const std::string db_path = "test_baas_config.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pkA = web::db::seed(db);
    db->execSqlSync("INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
                    std::string("B"), std::string("pk_b"), std::string("unset"));

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

        CHECK(http("GET", base + "/v1/config", {}).status == 401);   // no api-key

        const Resp all = http("GET", base + "/v1/config", {keyA});
        CHECK(all.status == 200);
        CHECK(parse(all.body)["config"]["motd"].asString() == "Welcome to Colony!");
        CHECK(parse(all.body)["config"]["max_agents"].asString() == "50");

        CHECK(parse(http("GET", base + "/v1/config/motd", {keyA}).body)["value"].asString() ==
              "Welcome to Colony!");
        CHECK(http("GET", base + "/v1/config/nope", {keyA}).status == 404);

        // tenant isolation: project B has no config
        CHECK(parse(http("GET", base + "/v1/config", {"X-Api-Key: pk_b"}).body)["config"].size() == 0);

        drogon::app().quit();
    });
    drogon::app().run();
    tester.join();
    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_config: all tests passed\n");
    else                 std::printf("baas_config: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
