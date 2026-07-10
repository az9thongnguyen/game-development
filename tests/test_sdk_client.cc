// =============================================================================
//  tests/test_sdk_client.cc  —  unit tests for the SDK client (no network)
// =============================================================================
//  Drives the Client through a FakeTransport that records the outgoing request and
//  returns a canned response on poll(). Proves request assembly, header/token
//  handling, response parsing, and the error path — all without a server.
// =============================================================================
#include <cstdio>
#include <memory>
#include <string>

#include "gbaas/gbaas.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

// Records the last request; returns a canned response when poll() is pumped.
struct FakeTransport : gbaas::ITransport {
    std::string         last_method, last_url, last_body;
    gbaas::Headers      last_headers;
    gbaas::HttpDone     pending;
    gbaas::HttpResponse canned;

    void send(const std::string& method, const std::string& url, const gbaas::Headers& headers,
              const std::string& body, gbaas::HttpDone done) override {
        last_method  = method;
        last_url     = url;
        last_headers = headers;
        last_body    = body;
        pending      = std::move(done);
    }
    void poll() override {
        if (pending) { auto d = std::move(pending); pending = nullptr; d(canned); }
    }
    void reply(int status, std::string body) { canned = {status, std::move(body)}; }
    bool header_is(const std::string& k, const std::string& v) const {
        for (const auto& h : last_headers)
            if (h.first == k && h.second == v) return true;
        return false;
    }
};

int main() {
    auto           owner = std::make_unique<FakeTransport>();
    FakeTransport* fake  = owner.get();
    gbaas::Client  c({"http://x", "pk_test"}, std::move(owner));

    // --- guest: request shape + api-key header + response parse + token store ---
    gbaas::Result<gbaas::Session> got{};
    bool                          called = false;
    c.auth().guest([&](gbaas::Result<gbaas::Session> r) { got = r; called = true; });
    CHECK(fake->last_method == "POST");
    CHECK(fake->last_url == "http://x/v1/auth/guest");
    CHECK(fake->header_is("X-Api-Key", "pk_test"));
    CHECK(!called);   // async: nothing fires until update()

    fake->reply(200, R"({"user":{"user_id":5,"display_name":"g","is_guest":true},"access_token":"tok"})");
    c.update();
    CHECK(called);
    CHECK(got);
    CHECK(got && got->user_id == 5 && got->is_guest && got->display_name == "g");
    CHECK(c.token() == "tok");

    // --- after auth, the bearer token attaches automatically ---
    c.leaderboard("colony_high").submit(100, [](gbaas::Result<gbaas::Rank>) {});
    CHECK(fake->last_method == "POST");
    CHECK(fake->last_url == "http://x/v1/leaderboards/colony_high/scores");
    CHECK(fake->last_body == R"({"value":100})");
    CHECK(fake->header_is("Authorization", "Bearer tok"));

    // --- string escaping when building bodies ---
    c.auth().registerUser("x@y.com", "pw", R"(Bob "The" Builder)",
                          [](gbaas::Result<gbaas::Session>) {});
    CHECK(fake->last_body.find(R"(\"The\")") != std::string::npos);

    // --- error envelope → Result error ---
    gbaas::Result<gbaas::Session> lg{};
    c.auth().login("a@b.com", "pw", [&](gbaas::Result<gbaas::Session> r) { lg = r; });
    CHECK(fake->last_body.find("a@b.com") != std::string::npos);
    fake->reply(401, R"({"error":{"code":"invalid_credentials","message":"nope"}})");
    c.update();
    CHECK(!lg);
    CHECK(lg.error && lg.error->status == 401 && lg.error->code == "invalid_credentials");

    // --- top: array parsing ---
    gbaas::Result<gbaas::Board> bd{};
    c.leaderboard("colony_high").top(10, [&](gbaas::Result<gbaas::Board> r) { bd = r; });
    CHECK(fake->last_method == "GET");
    CHECK(fake->last_url.find("/top?limit=10") != std::string::npos);
    fake->reply(200,
                R"({"entries":[{"rank":1,"user_id":5,"display_name":"g","value":200},)"
                R"({"rank":2,"user_id":6,"display_name":"h","value":100}]})");
    c.update();
    CHECK(bd);
    CHECK(bd && bd->entries.size() == 2);
    CHECK(bd && bd->entries[0].rank == 1 && bd->entries[0].value == 200 &&
          bd->entries[0].display_name == "g");

    // --- transport failure → error ---
    gbaas::Result<gbaas::Rank> mr{};
    c.leaderboard("colony_high").me([&](gbaas::Result<gbaas::Rank> r) { mr = r; });
    fake->reply(-1, "");
    c.update();
    CHECK(!mr && mr.error && mr.error->status == -1);

    // --- saves (cloud save) ---
    gbaas::Result<gbaas::SaveMeta> sm{};
    c.saves().put("slot1", R"(hello "world")", [&](gbaas::Result<gbaas::SaveMeta> r) { sm = r; });
    CHECK(fake->last_method == "PUT");
    CHECK(fake->last_url == "http://x/v1/saves/slot1");
    CHECK(fake->last_body.find(R"(\"world\")") != std::string::npos);   // payload escaped
    fake->reply(200, R"({"slot":"slot1","version":3,"size":13})");
    c.update();
    CHECK(sm && sm->version == 3 && sm->size == 13);

    gbaas::Result<gbaas::Save> sv{};
    c.saves().get("slot1", [&](gbaas::Result<gbaas::Save> r) { sv = r; });
    CHECK(fake->last_method == "GET");
    fake->reply(200, R"({"slot":"slot1","version":3,"data":"hello"})");
    c.update();
    CHECK(sv && sv->data == "hello" && sv->version == 3);

    gbaas::Result<std::vector<gbaas::SaveMeta>> sl{};
    c.saves().list([&](gbaas::Result<std::vector<gbaas::SaveMeta>> r) { sl = r; });
    fake->reply(200, R"({"saves":[{"slot":"a","version":1,"size":2},{"slot":"b","version":5,"size":9}]})");
    c.update();
    CHECK(sl && sl->size() == 2 && (*sl)[1].version == 5);

    gbaas::Result<bool> rm{};
    c.saves().remove("slot1", [&](gbaas::Result<bool> r) { rm = r; });
    CHECK(fake->last_method == "DELETE");
    fake->reply(200, R"({"deleted":true})");
    c.update();
    CHECK(rm && *rm == true);

    // --- JSON parser hardening (adversarial input must fail, not crash) ---
    {
        const std::string deep(5000, '[');                        // deeply nested → depth-capped
        CHECK(!gbaas::json::parse(deep).has_value());
        CHECK(!gbaas::json::parse(R"("\uD800A")").has_value());  // high surrogate, bad low
        CHECK(!gbaas::json::parse(R"("\uDC00")").has_value());        // lone low surrogate
        const auto big = gbaas::json::parse(R"({"n":9007199254740993})");  // 2^53+1 (64-bit)
        CHECK(big && (*big)["n"].as_int() == 9007199254740993LL);          // no truncation
    }

    if (g_failures == 0) std::printf("sdk_client: all tests passed\n");
    else                 std::printf("sdk_client: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
