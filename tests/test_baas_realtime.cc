// =============================================================================
//  tests/test_baas_realtime.cc  —  integration tests for the realtime tier
// =============================================================================
//  Drives the /v1/ws WebSocket endpoint with Drogon's own WebSocketClient (no new
//  dependency). Covers: lobby join → members + peer_joined; broadcast reaches
//  peers but not the sender; matchmaking pairs two waiters into one match room;
//  cross-tenant isolation (project B never sees A's room); and upgrade auth
//  rejection (bad token → error frame).
//
//  The server runs on the main thread (app().run()); the WebSocket clients live
//  on a separate trantor EventLoopThread, so client and server never share a loop.
//  A control thread waits for the server, mints guest tokens over HTTP, then drives
//  the sockets and asserts, and finally calls app().quit().
// =============================================================================
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/WebSocketClient.h>
#include <drogon/drogon.h>
#include <sodium.h>
#include <trantor/net/EventLoopThread.h>

#include "baas/app_config.h"
#include "baas/app_setup.h"
#include "baas/db/db.h"
#include "baas/realtime/hub.h"
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

// A test-side WebSocket peer: collects received text frames into a thread-safe
// inbox that the control thread waits on with a predicate + timeout.
struct WsPeer {
    drogon::WebSocketClientPtr client;
    std::mutex                 m;
    std::condition_variable    cv;
    std::vector<std::string>   inbox;

    void push(std::string s) {
        std::lock_guard<std::mutex> lk(m);
        inbox.push_back(std::move(s));
        cv.notify_all();
    }
    // Wait until some frame in the inbox satisfies pred (rescanned on each wake).
    bool wait_for(const std::function<bool(const Json::Value&)>& pred, int ms = 2000) {
        std::unique_lock<std::mutex> lk(m);
        return cv.wait_for(lk, std::chrono::milliseconds(ms), [&] {
            for (const auto& s : inbox)
                if (pred(parse(s))) return true;
            return false;
        });
    }
    // True if NO frame so far satisfies pred (call after a settle delay).
    bool none_match(const std::function<bool(const Json::Value&)>& pred) {
        std::lock_guard<std::mutex> lk(m);
        for (const auto& s : inbox)
            if (pred(parse(s))) return false;
        return true;
    }
};

static trantor::EventLoop* g_client_loop = nullptr;

// Open an authenticated WS connection; returns the peer once the HTTP upgrade
// completed (Ok even for auth-rejected sockets — the app-level error arrives as a
// frame after the 101, then the server closes).
static std::shared_ptr<WsPeer> open_ws(int port, const std::string& api_key,
                                       const std::string& token) {
    auto peer   = std::make_shared<WsPeer>();
    peer->client =
        drogon::WebSocketClient::newWebSocketClient("127.0.0.1", port, false, g_client_loop);
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setPath("/v1/ws?api_key=" + api_key + "&token=" + token);
    peer->client->setMessageHandler(
        [peer](std::string&& msg, const drogon::WebSocketClientPtr&,
               const drogon::WebSocketMessageType& t) {
            if (t == drogon::WebSocketMessageType::Text) peer->push(std::move(msg));
        });
    std::promise<void> connected;
    auto               fut = connected.get_future();
    peer->client->connectToServer(
        req, [&connected](drogon::ReqResult, const drogon::HttpResponsePtr&,
                          const drogon::WebSocketClientPtr&) { connected.set_value(); });
    fut.wait_for(std::chrono::seconds(3));
    return peer;
}

// Marshal a send onto the client loop (trantor requires loop-thread access).
static void ws_send(const std::shared_ptr<WsPeer>& p, const std::string& s) {
    g_client_loop->runInLoop([p, s] {
        if (auto conn = p->client->getConnection()) conn->send(s);
    });
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    web::set_config(web::AppConfig{"integration-test-secret", 3600});

    const std::string db_path = "test_baas_realtime.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pkA = web::db::seed(db);   // project A = pk_demo_colony

    // A second tenant (project B) for the isolation check.
    db->execSqlSync("INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
                    std::string("Proj B"), std::string("pk_b"), std::string("unset"));
    const std::string pkB = "pk_b";

    web::rt::RealtimeHub::instance().reset();

    const int port = baastest::find_free_port();
    drogon::app().setLogLevel(trantor::Logger::kError);
    web::register_routes();
    drogon::app().addListener("127.0.0.1", port);

    // The WebSocket clients run on their own loop, separate from the server's.
    trantor::EventLoopThread client_loop_thread;
    client_loop_thread.run();
    g_client_loop = client_loop_thread.getLoop();

    std::thread tester([&] {
        const std::string base = "http://127.0.0.1:" + std::to_string(port);
        for (int i = 0; i < 200; ++i) {
            if (http("GET", base + "/healthz", {}).status == 200) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        // Mint guest tokens over HTTP (the same credential the WS upgrade needs).
        auto guest = [&](const std::string& pk) {
            const Resp r = http("POST", base + "/v1/auth/guest", {"X-Api-Key: " + pk}, "{}");
            return std::pair<std::string, long long>{parse(r.body)["access_token"].asString(),
                                                     parse(r.body)["user"]["user_id"].asInt64()};
        };
        const auto [tokA1, idA1] = guest(pkA);
        const auto [tokA2, idA2] = guest(pkA);
        const auto [tokB, idB]   = guest(pkB);

        auto ev_is = [](const std::string& name) {
            return [name](const Json::Value& j) { return j["ev"].asString() == name; };
        };

        // ---- auth rejection: a bad token yields an error frame ----
        {
            auto bad = open_ws(port, pkA, "not-a-jwt");
            CHECK(bad->wait_for(ev_is("error")));
            g_client_loop->runInLoop([bad] { if (auto c = bad->client->getConnection()) c->shutdown(); });
        }

        // ---- lobby: join → members, peer_joined, broadcast ----
        auto a1 = open_ws(port, pkA, tokA1);
        auto a2 = open_ws(port, pkA, tokA2);

        ws_send(a1, R"({"op":"join","room":"lobby"})");
        CHECK(a1->wait_for([&](const Json::Value& j) {
            return j["ev"].asString() == "joined" && j["room"].asString() == "lobby" &&
                   j["members"].size() == 1;
        }));

        ws_send(a2, R"({"op":"join","room":"lobby"})");
        CHECK(a2->wait_for([&](const Json::Value& j) {
            return j["ev"].asString() == "joined" && j["members"].size() == 2;
        }));
        // A1 hears that A2 joined.
        CHECK(a1->wait_for([&](const Json::Value& j) {
            return j["ev"].asString() == "peer_joined" && j["user_id"].asInt64() == idA2;
        }));

        // A1 broadcasts; A2 receives it, A1 does not (no echo).
        ws_send(a1, R"({"op":"msg","data":"hello"})");
        CHECK(a2->wait_for([&](const Json::Value& j) {
            return j["ev"].asString() == "msg" && j["from"].asInt64() == idA1 &&
                   j["data"].asString() == "hello";
        }));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CHECK(a1->none_match(ev_is("msg")));

        // ---- tenant isolation: B joins "lobby" but sees only itself ----
        auto b = open_ws(port, pkB, tokB);
        ws_send(b, R"({"op":"join","room":"lobby"})");
        CHECK(b->wait_for([&](const Json::Value& j) {
            return j["ev"].asString() == "joined" && j["members"].size() == 1 &&
                   j["members"][0]["user_id"].asInt64() == idB;
        }));
        // A's broadcast must not cross into B.
        ws_send(a1, R"({"op":"msg","data":"secret"})");
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CHECK(b->none_match([](const Json::Value& j) {
            return j["ev"].asString() == "msg" && j["data"].asString() == "secret";
        }));

        // ---- matchmaking: two fresh waiters get paired into one room ----
        const auto [tokM1, idM1] = guest(pkA);
        const auto [tokM2, idM2] = guest(pkA);
        (void)idM1;
        (void)idM2;
        auto m1 = open_ws(port, pkA, tokM1);
        auto m2 = open_ws(port, pkA, tokM2);
        ws_send(m1, R"({"op":"queue"})");
        std::this_thread::sleep_for(std::chrono::milliseconds(80));   // ensure FIFO order
        ws_send(m2, R"({"op":"queue"})");
        std::string room1, room2;
        CHECK(m1->wait_for([&](const Json::Value& j) {
            if (j["ev"].asString() == "matched") { room1 = j["room"].asString(); return true; }
            return false;
        }));
        CHECK(m2->wait_for([&](const Json::Value& j) {
            if (j["ev"].asString() == "matched") { room2 = j["room"].asString(); return true; }
            return false;
        }));
        CHECK(!room1.empty() && room1 == room2);   // same match room

        // Now paired, m1's broadcast reaches m2.
        ws_send(m1, R"({"op":"msg","data":"gg"})");
        CHECK(m2->wait_for([&](const Json::Value& j) {
            return j["ev"].asString() == "msg" && j["data"].asString() == "gg";
        }));

        // ---- cancel: a lone waiter can leave the queue ----
        const auto [tokC3, idC3] = guest(pkA);
        (void)idC3;
        auto c3 = open_ws(port, pkA, tokC3);
        ws_send(c3, R"({"op":"queue"})");
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        CHECK(web::rt::RealtimeHub::instance().queue_size(1) == 1);   // project A id==1
        ws_send(c3, R"({"op":"cancel"})");
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        CHECK(web::rt::RealtimeHub::instance().queue_size(1) == 0);

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();
    client_loop_thread.getLoop()->quit();

    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("baas_realtime: all tests passed\n");
    else                 std::printf("baas_realtime: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
