// =============================================================================
//  tests/test_baas_assets.cc  —  integration tests for the asset registry
// =============================================================================
//  put/get round-trip, version bump, list + ?kind= filter, delete, optimistic
//  concurrency (If-Match), name/kind validation, payload cap, api-key-only access
//  (NO per-user JWT), and cross-tenant isolation. Assets are PROJECT-scoped.
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

    const std::string db_path = "test_baas_assets.db";
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
        const std::string keyA   = "X-Api-Key: " + pkA;
        const std::string keyB   = "X-Api-Key: " + pkB;
        const std::string assets = base + "/v1/assets";

        // api-key required (no key -> rejected, not 200) — but NO user JWT needed
        CHECK(http("GET", assets, {}).status != 200);

        // put -> get round-trip (data + kind), no Bearer token anywhere
        Resp p = http("PUT", assets + "/level_00.map", {keyA},
                      R"({"kind":"level","data":"fpsmap1\nsize 2 1\nrow 1 1\n"})");
        CHECK(p.status == 200);
        CHECK(parse(p.body)["version"].asInt() == 1);
        CHECK(parse(p.body)["kind"].asString() == "level");
        Resp g = http("GET", assets + "/level_00.map", {keyA});
        CHECK(g.status == 200);
        CHECK(parse(g.body)["data"].asString() == "fpsmap1\nsize 2 1\nrow 1 1\n");
        CHECK(parse(g.body)["version"].asInt() == 1);

        // update bumps the version
        p = http("PUT", assets + "/level_00.map", {keyA}, R"({"kind":"level","data":"v2"})");
        CHECK(parse(p.body)["version"].asInt() == 2);
        CHECK(parse(http("GET", assets + "/level_00.map", {keyA}).body)["data"].asString() == "v2");

        // a second asset of a different kind
        http("PUT", assets + "/wall_1.hrt", {keyA}, R"({"kind":"texture","data":"BASE64BYTES"})");

        // list all (data not included), then filter by kind
        Resp l = http("GET", assets, {keyA});
        CHECK(parse(l.body)["assets"].size() == 2);
        Resp lt = http("GET", assets + "?kind=texture", {keyA});
        CHECK(parse(lt.body)["assets"].size() == 1);
        CHECK(parse(lt.body)["assets"][0]["name"].asString() == "wall_1.hrt");

        // optimistic concurrency: wrong If-Match -> 409, correct -> 200
        CHECK(http("PUT", assets + "/level_00.map", {keyA, "If-Match: 99"}, R"({"data":"z"})").status == 409);
        CHECK(http("PUT", assets + "/level_00.map", {keyA, "If-Match: 2"}, R"({"data":"z"})").status == 200);

        // cross-tenant isolation: project B sees nothing of A's
        CHECK(parse(http("GET", assets, {keyB}).body)["assets"].size() == 0);
        CHECK(http("GET", assets + "/level_00.map", {keyB}).status == 404);

        // validation: bad name -> 400; bad kind -> 400; oversized -> 413
        CHECK(http("PUT", assets + "/bad*name", {keyA}, R"({"data":"x"})").status == 400);
        CHECK(http("PUT", assets + "/ok.name", {keyA}, R"({"kind":"has space","data":"x"})").status == 400);
        const std::string big(1100 * 1024, 'x');
        CHECK(http("PUT", assets + "/huge.bin", {keyA}, R"({"data":")" + big + R"("})").status == 413);

        // delete -> gone
        CHECK(http("DELETE", assets + "/wall_1.hrt", {keyA}).status == 200);
        CHECK(http("GET", assets + "/wall_1.hrt", {keyA}).status == 404);
        CHECK(http("DELETE", assets + "/wall_1.hrt", {keyA}).status == 404);

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_assets: all tests passed\n");
    else                 std::printf("baas_assets: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
