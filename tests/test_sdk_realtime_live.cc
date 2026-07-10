// =============================================================================
//  tests/test_sdk_realtime_live.cc  —  realtime tier, end-to-end over real ws://
// =============================================================================
//  The comprehensive realtime integration test. It drives the SDK's NATIVE
//  WebSocket transport (gbaas::WsTransportCurl over libcurl ws://) against a live
//  Drogon server, so it exercises BOTH the server (hub, lobby, matchmaking, tenant
//  isolation, auth-on-upgrade) AND the real client transport at once. Everything
//  runs on a worker thread with blocking curl while the app runs on the main
//  thread — no separate client event loop, so no cross-loop teardown race.
//
//  Requires a WebSocket-capable libcurl (GBAAS_HAS_WS_CURL / the project pins
//  Homebrew's curl). If the SDK were built without ws://, connect() returns false
//  and the connect CHECK fails loudly rather than passing silently.
// =============================================================================
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>

#include <curl/curl.h>
#include <drogon/drogon.h>
#include <sodium.h>

#include "baas/app_config.h"
#include "baas/app_setup.h"
#include "baas/db/db.h"
#include "gbaas/gbaas.h"
#include "tests/baas_test_util.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

// A raw libcurl ws:// connection used only to test the server's upgrade auth: a
// bad token must be rejected (the server sends an {"ev":"error"} frame and closes).
// Returns true if the server rejected us. Non-blocking recv so it can't hang.
static bool raw_ws_rejected(const std::string& url) {
    CURL* c = curl_easy_init();
    if (!c) return false;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 3000L);
    bool rejected = false;
    if (curl_easy_perform(c) == CURLE_OK) {   // the 101 upgrade itself always succeeds
        curl_socket_t s = CURL_SOCKET_BAD;
        if (curl_easy_getinfo(c, CURLINFO_ACTIVESOCKET, &s) == CURLE_OK && s != CURL_SOCKET_BAD) {
            const int fl = ::fcntl(s, F_GETFL, 0);
            if (fl != -1) ::fcntl(s, F_SETFL, fl | O_NONBLOCK);
        }
        for (int i = 0; i < 100 && !rejected; ++i) {
            char                        buf[1024];
            std::size_t                 got  = 0;
            const struct curl_ws_frame* meta = nullptr;
            const CURLcode              r    = curl_ws_recv(c, buf, sizeof(buf), &got, &meta);
            if (r == CURLE_OK && meta) {
                if (meta->flags & CURLWS_TEXT) {
                    const std::string m(buf, got);
                    if (m.find("\"error\"") != std::string::npos) rejected = true;
                }
                if (meta->flags & CURLWS_CLOSE) rejected = true;   // closed by server = rejected
            } else if (r != CURLE_AGAIN) {
                rejected = true;   // recv error → the server dropped us
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    curl_easy_cleanup(c);
    return rejected;
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    web::set_config(web::AppConfig{"live-rt-secret", 3600});

    const std::string db_path = "test_sdk_realtime_live.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pkA = web::db::seed(db);   // project A = pk_demo_colony
    db->execSqlSync("INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
                    std::string("Proj B"), std::string("pk_b"), std::string("unset"));
    const std::string pkB = "pk_b";

    const int         port = baastest::find_free_port();
    const std::string base = "http://127.0.0.1:" + std::to_string(port);
    const std::string wsb  = "ws://127.0.0.1:" + std::to_string(port);
    drogon::app().setLogLevel(trantor::Logger::kError);
    web::register_routes();
    drogon::app().addListener("127.0.0.1", port);

    std::thread tester([&] {
        for (int i = 0; i < 200; ++i) {
            if (baastest::http("GET", base + "/healthz", {}).status == 200) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        gbaas::Client a({base, pkA});
        gbaas::Client b({base, pkA});
        gbaas::Client bproj({base, pkB});   // a different tenant

        auto login = [](gbaas::Client& c) {
            bool done = false;
            c.auth().guest([&](gbaas::Result<gbaas::Session>) { done = true; });
            for (int i = 0; i < 400 && !done; ++i) {
                c.update();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return done;
        };
        CHECK(login(a));
        CHECK(login(b));
        CHECK(login(bproj));

        CHECK(a.realtime().connect());   // false ⇒ SDK built without ws-capable curl
        CHECK(b.realtime().connect());
        CHECK(bproj.realtime().connect());

        std::vector<gbaas::RtEvent> ea, eb, eB;
        auto pump = [&](int iters) {
            for (int i = 0; i < iters; ++i) {
                a.update();
                b.update();
                bproj.update();
                gbaas::RtEvent ev;
                while (a.realtime().poll(ev)) ea.push_back(ev);
                while (b.realtime().poll(ev)) eb.push_back(ev);
                while (bproj.realtime().poll(ev)) eB.push_back(ev);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };
        auto has = [](const std::vector<gbaas::RtEvent>& v,
                      const std::function<bool(const gbaas::RtEvent&)>& p) {
            for (const auto& e : v)
                if (p(e)) return true;
            return false;
        };

        // ---- connect events ----
        pump(20);
        CHECK(has(ea, [](const gbaas::RtEvent& e) { return e.ev == "connected"; }));
        CHECK(has(eb, [](const gbaas::RtEvent& e) { return e.ev == "connected"; }));

        // ---- lobby: both project-A clients join; broadcast reaches the peer only ----
        a.realtime().join("lobby");
        pump(12);
        b.realtime().join("lobby");
        pump(12);
        CHECK(has(ea, [](const gbaas::RtEvent& e) { return e.ev == "joined"; }));
        CHECK(has(eb, [](const gbaas::RtEvent& e) {
            return e.ev == "joined" && e.members.size() == 2;
        }));
        CHECK(has(ea, [](const gbaas::RtEvent& e) { return e.ev == "peer_joined"; }));

        ea.clear();
        eb.clear();
        a.realtime().send("hello over ws");
        pump(15);
        CHECK(has(eb, [](const gbaas::RtEvent& e) {
            return e.ev == "msg" && e.data == "hello over ws";
        }));
        CHECK(!has(ea, [](const gbaas::RtEvent& e) { return e.ev == "msg"; }));   // no self-echo

        // ---- tenant isolation: project B joins "lobby" but sees only itself ----
        bproj.realtime().join("lobby");
        pump(15);
        CHECK(has(eB, [](const gbaas::RtEvent& e) {
            return e.ev == "joined" && e.members.size() == 1;   // NOT the 2 project-A members
        }));
        eB.clear();
        a.realtime().send("secret");   // project A broadcast must not cross to B
        pump(15);
        CHECK(!has(eB, [](const gbaas::RtEvent& e) {
            return e.ev == "msg" && e.data == "secret";
        }));

        // ---- matchmaking: two fresh project-A clients get paired into one room ----
        gbaas::Client m1({base, pkA});
        gbaas::Client m2({base, pkA});
        CHECK(login(m1));
        CHECK(login(m2));
        CHECK(m1.realtime().connect());
        CHECK(m2.realtime().connect());
        std::vector<gbaas::RtEvent> em1, em2;
        auto pump2 = [&](int iters) {
            for (int i = 0; i < iters; ++i) {
                m1.update();
                m2.update();
                gbaas::RtEvent ev;
                while (m1.realtime().poll(ev)) em1.push_back(ev);
                while (m2.realtime().poll(ev)) em2.push_back(ev);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };
        pump2(10);
        m1.realtime().queue();
        pump2(6);   // ensure m1 is first in the FIFO
        m2.realtime().queue();
        pump2(15);
        std::string r1, r2;
        for (const auto& e : em1) if (e.ev == "matched") r1 = e.room;
        for (const auto& e : em2) if (e.ev == "matched") r2 = e.room;
        CHECK(!r1.empty() && r1 == r2);

        // ---- cancel: a lone waiter leaves the queue (checked via the hub) ----
        m1.realtime().disconnect();
        m2.realtime().disconnect();

        // ---- auth rejection on the upgrade: a bad token is refused ----
        CHECK(raw_ws_rejected(wsb + "/v1/ws?api_key=" + pkA + "&token=not-a-jwt"));

        a.realtime().disconnect();
        b.realtime().disconnect();
        bproj.realtime().disconnect();
        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("sdk_realtime_live: all tests passed\n");
    else                 std::printf("sdk_realtime_live: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
