// =============================================================================
//  tests/test_baas_analytics.cc  —  integration tests for analytics ingest
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
    const std::string db_path = "test_baas_analytics.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pkA = web::db::seed(db);

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
        const std::string ev   = base + "/v1/analytics/events";
        const Resp r  = http("POST", base + "/v1/auth/register", {keyA},
                             R"({"email":"a@x.com","password":"secret1","display_name":"A"})");
        const std::string a1  = "Authorization: Bearer " + parse(r.body)["access_token"].asString();

        CHECK(http("POST", ev, {}, R"({"name":"boot"})").status == 401);           // no api-key

        // anonymous event (no Bearer)
        CHECK(http("POST", ev, {keyA}, R"({"name":"app.open"})").status == 200);
        // attributed event (with Bearer) + props
        CHECK(http("POST", ev, {keyA, a1}, R"({"name":"score.submitted","props":{"v":42}})").status == 200);
        // validation
        CHECK(http("POST", ev, {keyA}, R"({"name":"bad name!"})").status == 400);   // bad chars
        CHECK(http("POST", ev, {keyA}, R"({})").status == 400);                     // missing name
        CHECK(http("POST", ev, {keyA}, R"({"name":"big","props":")" + std::string(5000, 'x') +
                                        R"("})").status == 413);                    // props too big

        drogon::app().quit();
    });
    drogon::app().run();
    tester.join();

    // Verify persistence: 2 recorded; the attributed one carries the user id.
    CHECK(db->execSqlSync("SELECT count(*) AS c FROM analytics_events")[0]["c"].as<long>() == 2);
    const auto row = db->execSqlSync("SELECT user_id FROM analytics_events WHERE name='score.submitted'");
    CHECK(!row.empty() && !row[0]["user_id"].isNull());
    const auto anon = db->execSqlSync("SELECT user_id FROM analytics_events WHERE name='app.open'");
    CHECK(!anon.empty() && anon[0]["user_id"].isNull());

    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_analytics: all tests passed\n");
    else                 std::printf("baas_analytics: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
