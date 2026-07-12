// =============================================================================
//  tests/test_baas_tracing.cc  —  request correlation ids (H2 telemetry)
// =============================================================================
//  Every response carries an X-Request-Id: a sanitized inbound one if the caller sent
//  it (distributed tracing across a proxy), or a freshly minted one otherwise. This is
//  what makes a log line traceable back to a single request. Boots a real server.
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

using baastest::header_value;
using baastest::http;
using baastest::Resp;

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

static bool is_hex(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    web::set_config(web::AppConfig{"integration-test-secret", 3600, "test-admin"});

    const std::string db_path = "test_baas_tracing.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    web::db::seed(db);

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

        // No inbound id → the server mints one: present, non-empty, 16 hex chars.
        const Resp a = http("GET", base + "/healthz", {});
        const std::string id_a = header_value(a, "X-Request-Id");
        CHECK(a.status == 200);
        CHECK(id_a.size() == 16 && is_hex(id_a));

        // A second no-id request gets a DIFFERENT minted id (they are unique per request).
        const Resp b = http("GET", base + "/healthz", {});
        CHECK(header_value(b, "X-Request-Id") != id_a);

        // An inbound id is adopted and echoed verbatim (distributed tracing across a proxy).
        const Resp c = http("GET", base + "/healthz", {"X-Request-Id: trace-abc_123"});
        CHECK(header_value(c, "X-Request-Id") == "trace-abc_123");

        // A hostile inbound id is sanitized (anything outside [A-Za-z0-9_-] → '_'), so it
        // cannot forge a log line or inject into the echoed header.
        const Resp d = http("GET", base + "/healthz", {"X-Request-Id: id with/special*chars"});
        CHECK(header_value(d, "X-Request-Id") == "id_with_special_chars");

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();
    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_tracing: all tests passed\n");
    else                 std::printf("baas_tracing: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
