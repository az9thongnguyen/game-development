// =============================================================================
//  tests/test_baas_auth.cc  —  integration tests for the BaaS HTTP layer
// =============================================================================
//  Boots the real Drogon app on an ephemeral port against a temp SQLite DB and
//  drives it with libcurl. This file owns the shared harness (http(), free-port,
//  temp-db) that later auth/leaderboard cases build on. The app runs on the main
//  thread; a worker thread makes the requests and then calls app().quit().
//
//  S1.2 scope: the gateway — every /v1 route requires a valid X-Api-Key.
// =============================================================================
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <memory>

#include <curl/curl.h>
#include <drogon/drogon.h>
#include <json/json.h>
#include <sodium.h>

#include "baas/app_config.h"
#include "baas/app_setup.h"
#include "baas/db/db.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

// ---- libcurl HTTP driver (synchronous — perfect for a test) ------------------
static size_t write_cb(char* p, size_t sz, size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, sz * n);
    return sz * n;
}
struct Resp {
    long        status = 0;
    std::string body;
};
static Resp http(const std::string& method, const std::string& url,
                 const std::vector<std::string>& headers,
                 const std::string& body = "") {
    Resp  r;
    CURL* c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
    struct curl_slist* hs = nullptr;
    for (const auto& h : headers) hs = curl_slist_append(hs, h.c_str());
    if (!body.empty()) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
        hs = curl_slist_append(hs, "Content-Type: application/json");
    }
    if (hs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
    if (curl_easy_perform(c) != CURLE_OK) r.status = -1;
    else curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    if (hs) curl_slist_free_all(hs);
    curl_easy_cleanup(c);
    return r;
}

// Ask the OS for a free loopback port (tiny race window, fine for a local test).
static int find_free_port() {
    int         s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;
    ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    const int port = ntohs(a.sin_port);
    ::close(s);
    return port;
}

static void cleanup_db(const std::string& path) {
    for (const char* suffix : {"", "-journal", "-wal", "-shm"})
        std::remove((path + suffix).c_str());
}

static Json::Value parse(const std::string& body) {
    Json::Value                             j;
    Json::CharReaderBuilder                 rb;
    std::string                             e;
    const std::unique_ptr<Json::CharReader> r(rb.newCharReader());
    r->parse(body.data(), body.data() + body.size(), &j, &e);
    return j;
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    web::set_config(web::AppConfig{"integration-test-secret", 3600});
    const std::string db_path = "test_baas_auth.db";
    cleanup_db(db_path);

    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pk = web::db::seed(db);

    const int         port = find_free_port();
    const std::string base = "http://127.0.0.1:" + std::to_string(port);

    drogon::app().setLogLevel(trantor::Logger::kError);   // quiet during tests
    web::register_routes();
    drogon::app().addListener("127.0.0.1", port);

    std::thread tester([&] {
        // Wait until the listener is accepting connections.
        for (int i = 0; i < 200; ++i) {
            if (http("GET", base + "/healthz", {}).status == 200) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        CHECK(http("GET", base + "/healthz", {}).status == 200);

        // --- the gateway (ApiKeyFilter) ---
        CHECK(http("GET", base + "/v1/ping", {}).status == 401);                   // no key
        CHECK(http("GET", base + "/v1/ping", {"X-Api-Key: nope"}).status == 401);  // bad key
        const Resp ok = http("GET", base + "/v1/ping", {"X-Api-Key: " + pk});
        CHECK(ok.status == 200);                                                   // valid key
        CHECK(ok.body.find("project_id") != std::string::npos);                    // project resolved

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

    drogon::app().run();   // blocks until tester calls quit()
    tester.join();

    cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_auth: all tests passed\n");
    else                 std::printf("baas_auth: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
