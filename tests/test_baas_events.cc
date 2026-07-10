// =============================================================================
//  tests/test_baas_events.cc  —  integration tests for live events (read)
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
    const std::string db_path = "test_baas_events.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pkA = web::db::seed(db);
    const auto insB = db->execSqlSync(
        "INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
        std::string("B"), std::string("pk_b"), std::string("unset"));
    // An EXPIRED event for A must NOT appear as active.
    db->execSqlSync(
        "INSERT INTO live_events(project_id, key, name, starts_at, ends_at, payload) VALUES(?,?,?,?,?,?)",
        1L, std::string("old"), std::string("Old Event"),
        std::string("2000-01-01 00:00:00"), std::string("2000-02-01 00:00:00"), std::string("{}"));

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

        CHECK(http("GET", base + "/v1/events", {}).status == 401);   // no api-key

        const Resp ev = http("GET", base + "/v1/events", {keyA});
        CHECK(ev.status == 200);
        const auto arr = parse(ev.body)["events"];
        CHECK(arr.size() == 1);                                      // only the active one
        CHECK(arr[0]["key"].asString() == "double_wood");
        CHECK(arr[0]["name"].asString() == "Double Wood Weekend");

        // tenant isolation: project B has no events
        CHECK(parse(http("GET", base + "/v1/events", {"X-Api-Key: pk_b"}).body)["events"].size() == 0);

        drogon::app().quit();
    });
    drogon::app().run();
    tester.join();
    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_events: all tests passed\n");
    else                 std::printf("baas_events: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
