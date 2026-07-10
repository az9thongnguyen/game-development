// =============================================================================
//  tests/test_baas_ratelimit.cc  —  integration test for gateway rate limiting
// =============================================================================
//  With a low, no-refill limit (capacity 3), a caller's 4th /v1 request in a burst
//  is rejected with 429; /healthz is never throttled; and a different api-key has
//  its own bucket (so it isn't affected — it reaches the api-key check and 401s
//  rather than 429s).
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
    // capacity 3, no refill → a burst of 3 passes, then throttled (deterministic).
    web::set_config(web::AppConfig{"integration-test-secret", 3600, "", 3.0, 0.0});

    const std::string db_path = "test_baas_ratelimit.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pk = web::db::seed(db);

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
        const std::string key = "X-Api-Key: " + pk;

        // ---- burst of 3 on /v1/ping passes, the 4th is 429 ----
        CHECK(http("GET", base + "/v1/ping", {key}).status == 200);
        CHECK(http("GET", base + "/v1/ping", {key}).status == 200);
        CHECK(http("GET", base + "/v1/ping", {key}).status == 200);
        const Resp over = http("GET", base + "/v1/ping", {key});
        CHECK(over.status == 429);
        CHECK(parse(over.body)["error"]["code"].asString() == "rate_limited");

        // ---- /healthz is never throttled (not a /v1 route) ----
        bool health_ok = true;
        for (int i = 0; i < 10; ++i)
            if (http("GET", base + "/healthz", {}).status != 200) health_ok = false;
        CHECK(health_ok);

        // ---- a different api-key has its own bucket: it passes rate limiting and
        //      reaches the api-key check (401 for an invalid key), NOT 429 ----
        const Resp other = http("GET", base + "/v1/ping", {"X-Api-Key: some-other-key"});
        CHECK(other.status == 401);   // reached ApiKeyFilter → invalid, but not throttled

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();
    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_ratelimit: all tests passed\n");
    else                 std::printf("baas_ratelimit: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
