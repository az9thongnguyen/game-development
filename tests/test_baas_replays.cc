// =============================================================================
//  tests/test_baas_replays.cc  —  integration tests for the replay store
// =============================================================================
//  Create/list/get/delete of named, immutable per-user replays. Covers the
//  authenticated-write rule, newest-first listing, per-user isolation (a player
//  never sees or deletes another's replays), validation, and the size cap.
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

    const std::string db_path = "test_baas_replays.db";
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
        const std::string url = base + "/v1/replays";

        auto guest = [&] {
            const Resp r = http("POST", base + "/v1/auth/guest", {key}, "{}");
            return "Authorization: Bearer " + parse(r.body)["access_token"].asString();
        };
        const std::string u1 = guest();
        const std::string u2 = guest();

        // ---- writes require a JWT ----
        CHECK(http("POST", url, {key}, R"({"name":"x","data":"y"})").status == 401);

        // ---- U1 creates two replays ----
        const Resp a = http("POST", url, {key, u1}, R"({"name":"run-a","data":"cmd:spawn;cmd:goal"})");
        CHECK(a.status == 200);
        const long long ida = parse(a.body)["id"].asInt64();
        CHECK(ida > 0 && parse(a.body)["size"].asInt64() == 18);   // "cmd:spawn;cmd:goal"
        const Resp b = http("POST", url, {key, u1}, R"({"name":"run-b","data":"cmd:reset"})");
        const long long idb = parse(b.body)["id"].asInt64();
        CHECK(idb > ida);

        // ---- list is newest-first ----
        const auto lst = parse(http("GET", url, {key, u1}).body);
        CHECK(lst["replays"].size() == 2);
        CHECK(lst["replays"][0]["id"].asInt64() == idb);   // newest first
        CHECK(lst["replays"][1]["id"].asInt64() == ida);
        CHECK(lst["replays"][1]["name"].asString() == "run-a");

        // ---- get by id returns the payload ----
        const Resp ga = http("GET", url + "/" + std::to_string(ida), {key, u1});
        CHECK(ga.status == 200);
        CHECK(parse(ga.body)["data"].asString() == "cmd:spawn;cmd:goal");

        // ---- per-user isolation: U2 sees nothing, can't read/delete U1's ----
        CHECK(parse(http("GET", url, {key, u2}).body)["replays"].size() == 0);
        CHECK(http("GET", url + "/" + std::to_string(ida), {key, u2}).status == 404);
        CHECK(http("DELETE", url + "/" + std::to_string(ida), {key, u2}).status == 404);

        // ---- validation ----
        CHECK(http("POST", url, {key, u1}, R"({"name":"","data":"z"})").status == 400);   // empty name
        CHECK(http("POST", url, {key, u1}, R"({"data":"z"})").status == 400);             // no name
        {
            const std::string big(600 * 1024, 'x');
            CHECK(http("POST", url, {key, u1},
                       R"({"name":"big","data":")" + big + R"("})").status == 413);       // > 512 KiB
        }

        // ---- delete, then it's gone ----
        CHECK(http("DELETE", url + "/" + std::to_string(ida), {key, u1}).status == 200);
        CHECK(http("GET", url + "/" + std::to_string(ida), {key, u1}).status == 404);
        CHECK(parse(http("GET", url, {key, u1}).body)["replays"].size() == 1);
        CHECK(http("DELETE", url + "/" + std::to_string(ida), {key, u1}).status == 404);  // already gone

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();
    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_replays: all tests passed\n");
    else                 std::printf("baas_replays: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
