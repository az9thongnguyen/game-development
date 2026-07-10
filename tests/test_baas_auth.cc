// =============================================================================
//  tests/test_baas_auth.cc  —  integration tests for the gateway + auth
// =============================================================================
//  Boots the real Drogon app on an ephemeral port against a temp SQLite DB and
//  drives it with libcurl (see baas_test_util.h). The app runs on the main
//  thread; a worker thread makes the requests and then calls app().quit().
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

    const std::string db_path = "test_baas_auth.db";
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
        CHECK(http("GET", base + "/healthz", {}).status == 200);

        // --- gateway (ApiKeyFilter) ---
        CHECK(http("GET", base + "/v1/ping", {}).status == 401);                   // no key
        CHECK(http("GET", base + "/v1/ping", {"X-Api-Key: nope"}).status == 401);  // bad key
        const Resp ping = http("GET", base + "/v1/ping", {"X-Api-Key: " + pk});
        CHECK(ping.status == 200);                                                 // valid key
        CHECK(ping.body.find("project_id") != std::string::npos);                  // project resolved

        // --- auth: register / login / guest (all behind the api-key) ---
        const std::string key = "X-Api-Key: " + pk;

        const Resp reg = http("POST", base + "/v1/auth/register", {key},
                              R"({"email":"a@b.com","password":"secret1","display_name":"Ann"})");
        CHECK(reg.status == 200);
        CHECK(!parse(reg.body)["access_token"].asString().empty());
        CHECK(parse(reg.body)["user"]["is_guest"].asBool() == false);

        // duplicate email -> 409
        CHECK(http("POST", base + "/v1/auth/register", {key},
                   R"({"email":"a@b.com","password":"secret1","display_name":"A2"})")
                  .status == 409);
        // password too short -> 400
        CHECK(http("POST", base + "/v1/auth/register", {key},
                   R"({"email":"c@d.com","password":"x","display_name":"C"})")
                  .status == 400);

        // login OK -> 200 + token
        const Resp login = http("POST", base + "/v1/auth/login", {key},
                                R"({"email":"a@b.com","password":"secret1"})");
        CHECK(login.status == 200);
        CHECK(!parse(login.body)["access_token"].asString().empty());

        // wrong password and unknown email give the SAME 401 (no enumeration)
        const Resp bad_pw = http("POST", base + "/v1/auth/login", {key},
                                 R"({"email":"a@b.com","password":"nope"})");
        const Resp no_user = http("POST", base + "/v1/auth/login", {key},
                                  R"({"email":"zz@zz.com","password":"whatever"})");
        CHECK(bad_pw.status == 401);
        CHECK(no_user.status == 401);
        CHECK(parse(bad_pw.body)["error"]["code"].asString() ==
              parse(no_user.body)["error"]["code"].asString());

        // guest -> 200, is_guest true, token present
        const Resp guest = http("POST", base + "/v1/auth/guest", {key}, R"({"display_name":"G"})");
        CHECK(guest.status == 200);
        CHECK(parse(guest.body)["user"]["is_guest"].asBool() == true);
        CHECK(!parse(guest.body)["access_token"].asString().empty());

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_auth: all tests passed\n");
    else                 std::printf("baas_auth: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
