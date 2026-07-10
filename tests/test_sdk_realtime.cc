// =============================================================================
//  tests/test_sdk_realtime.cc  —  unit tests for the SDK Realtime handle (no socket)
// =============================================================================
//  Drives gbaas::Realtime through a FAKE IWsTransport that records outgoing frames
//  and replays canned server frames on poll(). Proves op framing (join/msg/queue/
//  cancel/leave, with escaping), the connect() URL assembly, connected/disconnected
//  synthesis, and server-event parsing/dispatch — all deterministically, no network.
// =============================================================================
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "gbaas/gbaas.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

// Minimal fake HTTP transport, just enough to mint a token so connect() has one.
struct FakeHttp : gbaas::ITransport {
    gbaas::HttpDone     pending;
    gbaas::HttpResponse canned;
    void send(const std::string&, const std::string&, const gbaas::Headers&, const std::string&,
              gbaas::HttpDone done) override {
        pending = std::move(done);
    }
    void poll() override {
        if (pending) { auto d = std::move(pending); pending = nullptr; d(canned); }
    }
    void reply(int status, std::string body) { canned = {status, std::move(body)}; }
};

// Fake WebSocket transport: records sent frames; replays queued frames on poll().
struct FakeWs : gbaas::IWsTransport {
    bool                     open_called = false;
    std::string              url;
    bool                     is_connected = false;
    bool                     alive        = true;
    std::vector<std::string> sent;
    std::vector<std::string> incoming;   // delivered (and cleared) on next poll()

    bool open(const std::string& u) override {
        open_called  = true;
        url          = u;
        is_connected = true;
        return true;
    }
    void close() override { is_connected = false; }
    bool connected() const override { return is_connected; }
    bool send_text(const std::string& t) override { sent.push_back(t); return true; }
    bool poll(std::vector<std::string>& out) override {
        for (auto& s : incoming) out.push_back(std::move(s));
        incoming.clear();
        if (!alive) is_connected = false;   // a dropped socket is no longer connected
        return alive;
    }
};

int main() {
    // --- connect() refuses without a token ---
    {
        gbaas::Client   c0({"http://h", "pk"}, std::make_unique<FakeHttp>());
        gbaas::Realtime rt0(&c0, std::make_unique<FakeWs>());
        CHECK(!rt0.connect());   // no authenticated user yet
    }

    // --- set up a client with a token via the fake HTTP transport ---
    auto            httpOwner = std::make_unique<FakeHttp>();
    FakeHttp*       http      = httpOwner.get();
    gbaas::Client   c({"http://127.0.0.1:9999", "pk_test"}, std::move(httpOwner));
    c.auth().guest([](gbaas::Result<gbaas::Session>) {});
    http->reply(200,
                R"({"user":{"user_id":7,"display_name":"g","is_guest":true},"access_token":"jwt.tok.sig"})");
    c.update();
    CHECK(c.token() == "jwt.tok.sig");

    // --- connect(): URL assembly (http→ws, api_key + token in the query) ---
    auto            wsOwner = std::make_unique<FakeWs>();
    FakeWs*         ws      = wsOwner.get();
    gbaas::Realtime rt(&c, std::move(wsOwner));
    CHECK(rt.connect());
    CHECK(ws->open_called);
    CHECK(ws->url == "ws://127.0.0.1:9999/v1/ws?api_key=pk_test&token=jwt.tok.sig");
    CHECK(rt.connected());
    CHECK(rt.connect());   // idempotent

    // --- first update synthesizes a "connected" event ---
    rt.update();
    gbaas::RtEvent ev;
    CHECK(rt.poll(ev) && ev.ev == "connected");
    CHECK(!rt.poll(ev));   // drained

    // --- ops produce the right frames (with escaping) ---
    rt.join("lobby-1");
    CHECK(ws->sent.back() == R"({"op":"join","room":"lobby-1"})");
    rt.send(R"(hi "there")");
    CHECK(ws->sent.back() == R"({"op":"msg","data":"hi \"there\""})");
    rt.queue();
    CHECK(ws->sent.back() == R"({"op":"queue"})");
    rt.cancel();
    CHECK(ws->sent.back() == R"({"op":"cancel"})");
    rt.leave();
    CHECK(ws->sent.back() == R"({"op":"leave"})");

    // --- incoming server frames parse into events (malformed → error, no crash) ---
    ws->incoming = {
        R"({"ev":"joined","room":"lobby-1","members":[{"user_id":7,"name":"g"},{"user_id":8,"name":"h"}]})",
        R"({"ev":"peer_joined","user_id":9,"name":"i"})",
        R"({"ev":"msg","from":8,"name":"h","data":"yo"})",
        R"({"ev":"matched","room":"match_3"})",
        R"({"ev":"peer_left","user_id":8})",
        "not json",
    };
    rt.update();
    std::vector<gbaas::RtEvent> evs;
    while (rt.poll(ev)) evs.push_back(ev);
    CHECK(evs.size() == 6);
    CHECK(evs[0].ev == "joined" && evs[0].room == "lobby-1" && evs[0].members.size() == 2 &&
          evs[0].members[1].user_id == 8 && evs[0].members[1].name == "h");
    CHECK(evs[1].ev == "peer_joined" && evs[1].from == 9 && evs[1].name == "i");
    CHECK(evs[2].ev == "msg" && evs[2].from == 8 && evs[2].data == "yo");
    CHECK(evs[3].ev == "matched" && evs[3].room == "match_3");
    CHECK(evs[4].ev == "peer_left" && evs[4].from == 8);
    CHECK(evs[5].ev == "error" && evs[5].message == "malformed frame");

    // --- a dropped socket surfaces exactly one "disconnected" event ---
    ws->alive = false;
    rt.update();
    CHECK(rt.poll(ev) && ev.ev == "disconnected");
    CHECK(!rt.connected());
    rt.update();          // now inert
    CHECK(!rt.poll(ev));

    // --- client.realtime() is lazily created and stable (same instance) ---
    CHECK(&c.realtime() == &c.realtime());

    if (g_failures == 0) std::printf("sdk_realtime: all tests passed\n");
    else                 std::printf("sdk_realtime: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
