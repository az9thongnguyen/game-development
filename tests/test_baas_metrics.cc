// =============================================================================
//  tests/test_baas_metrics.cc  —  integration test for the /metrics endpoint
// =============================================================================
//  Every response is counted by a pre-sending advice; /metrics (admin-gated) reports
//  the totals. Verifies the admin gate, and that a known set of requests is tallied
//  correctly by status class and by normalized route.
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
#include "baas/observability/metrics.h"
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
    web::set_config(web::AppConfig{"integration-test-secret", 3600, "test-admin"});

    const std::string db_path = "test_baas_metrics.db";
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
        // Ignore the startup healthz probes: start counting from a clean slate.
        web::Metrics::instance().reset();

        // A known set of requests (each recorded by the pre-sending advice):
        CHECK(http("GET", base + "/healthz", {}).status == 200);                       // 2xx
        CHECK(http("GET", base + "/v1/ping", {}).status == 401);                       // 4xx (no api-key)
        CHECK(http("POST", base + "/v1/auth/guest", {"X-Api-Key: " + pk}, "{}").status == 200);  // 2xx
        CHECK(http("GET", base + "/metrics", {}).status == 401);                       // 4xx (no admin secret)

        // Scrape (admin-gated). Its snapshot reflects the four requests above; this
        // scrape itself is only counted afterward.
        const Resp m = http("GET", base + "/metrics", {"X-Admin-Secret: test-admin"});
        CHECK(m.status == 200);
        const auto j = parse(m.body);
        CHECK(j["total"].asInt64() == 4);
        CHECK(j["by_status"]["2xx"].asInt64() == 2);       // healthz + guest
        CHECK(j["by_status"]["4xx"].asInt64() == 2);       // ping + metrics-no-admin
        CHECK(j["by_path"]["/healthz"].asInt64() == 1);
        CHECK(j["by_path"]["/v1/auth"].asInt64() == 1);    // "/v1/auth/guest" → "/v1/auth"
        CHECK(j["by_path"]["/v1/ping"].asInt64() == 1);

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();
    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_metrics: all tests passed\n");
    else                 std::printf("baas_metrics: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
