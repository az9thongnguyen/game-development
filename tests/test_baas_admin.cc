// =============================================================================
//  tests/test_baas_admin.cc  —  integration tests for the dashboard admin API
// =============================================================================
//  Platform-admin (X-Admin-Secret) project create/list; project-admin
//  (X-Api-Key + X-Secret-Key) config/events/analytics/users; the auth gating and
//  cross-project secret rejection.
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
    web::set_config(web::AppConfig{"integration-test-secret", 3600, "test-admin"});
    const std::string db_path = "test_baas_admin.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pkA = web::db::seed(db);   // demo project: pk_demo_colony / sk_demo_colony

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
        const std::string keyA   = "X-Api-Key: " + pkA;
        const std::string secA   = "X-Secret-Key: sk_demo_colony";
        const std::string admin  = "X-Admin-Secret: test-admin";

        // ---- platform admin: create project (gated by the admin secret) ----
        CHECK(http("POST", base + "/v1/admin/projects", {}, R"({"name":"P2"})").status == 401);
        CHECK(http("POST", base + "/v1/admin/projects", {"X-Admin-Secret: wrong"},
                   R"({"name":"P2"})").status == 401);
        const Resp np = http("POST", base + "/v1/admin/projects", {admin}, R"({"name":"P2"})");
        CHECK(np.status == 200);
        const std::string pkB = parse(np.body)["public_key"].asString();
        const std::string skB = parse(np.body)["secret_key"].asString();
        CHECK(pkB.rfind("pk_", 0) == 0 && skB.rfind("sk_", 0) == 0);
        CHECK(parse(http("GET", base + "/v1/admin/projects", {admin}).body)["projects"].size() == 2);

        // ---- project admin: secret gating ----
        CHECK(http("PUT", base + "/v1/admin/config/difficulty", {keyA}, R"({"value":"hard"})").status == 401);
        CHECK(http("PUT", base + "/v1/admin/config/difficulty", {keyA, "X-Secret-Key: nope"},
                   R"({"value":"hard"})").status == 401);
        // cross-project: A's api-key + B's secret → rejected
        CHECK(http("PUT", base + "/v1/admin/config/difficulty", {keyA, "X-Secret-Key: " + skB},
                   R"({"value":"hard"})").status == 401);

        // ---- config set/delete → visible via the public read API ----
        CHECK(http("PUT", base + "/v1/admin/config/difficulty", {keyA, secA},
                   R"({"value":"hard"})").status == 200);
        CHECK(parse(http("GET", base + "/v1/config/difficulty", {keyA}).body)["value"].asString() == "hard");
        CHECK(http("DELETE", base + "/v1/admin/config/difficulty", {keyA, secA}).status == 200);
        CHECK(http("GET", base + "/v1/config/difficulty", {keyA}).status == 404);

        // ---- schedule a live event → visible via the public read API ----
        CHECK(http("POST", base + "/v1/admin/events", {keyA, secA},
                   R"({"key":"flash","name":"Flash Sale","starts_at":"2000-01-01 00:00:00",)"
                   R"("ends_at":"2999-01-01 00:00:00"})").status == 200);
        const auto evs = parse(http("GET", base + "/v1/events", {keyA}).body);
        bool       found = false;
        for (const auto& e : evs["events"])
            if (e["key"].asString() == "flash") found = true;
        CHECK(found);

        // ---- analytics summary + users list ----
        http("POST", base + "/v1/analytics/events", {keyA}, R"({"name":"app.open"})");
        http("POST", base + "/v1/auth/register", {keyA},
             R"({"email":"u@x.com","password":"secret1","display_name":"U"})");
        const auto summary = parse(http("GET", base + "/v1/admin/analytics/summary", {keyA, secA}).body);
        bool       has_event = false;
        for (const auto& c : summary["counts"])
            if (c["name"].asString() == "app.open") has_event = true;
        CHECK(has_event);
        CHECK(parse(http("GET", base + "/v1/admin/users", {keyA, secA}).body)["users"].size() == 1);

        drogon::app().quit();
    });
    drogon::app().run();
    tester.join();
    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_admin: all tests passed\n");
    else                 std::printf("baas_admin: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
