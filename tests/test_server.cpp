// =============================================================================
//  tests/test_server.cpp  —  webserver logic (no sockets): mime · paths · http ·
//                            leaderboard. Excludes net.cpp, so nothing binds a port.
// =============================================================================
#include "server/http.hpp"
#include "server/leaderboard.hpp"
#include "server/mime.hpp"
#include "server/static_files.hpp"

#include <cstdio>
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static std::string to_str(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }

static void test_mime() {
    CHECK(web::mime_for("demo.html") == "text/html; charset=utf-8");
    CHECK(web::mime_for("demo.js")   == "application/javascript");
    CHECK(web::mime_for("demo.wasm") == "application/wasm");      // the critical one
    CHECK(web::mime_for("demo.data") == "application/octet-stream");
    CHECK(web::mime_for("x.json")    == "application/json");
    CHECK(web::mime_for("noext")     == "application/octet-stream");
}

static void test_resolve() {
    CHECK(web::resolve("root", "/")            == std::optional<std::string>("root/demo.html"));
    CHECK(web::resolve("root", "/demo.wasm")   == std::optional<std::string>("root/demo.wasm"));
    CHECK(web::resolve("root", "/a/b.js")      == std::optional<std::string>("root/a/b.js"));
    CHECK(web::resolve("root", "/x?y=1&z=2")   == std::optional<std::string>("root/x"));   // query stripped
    // traversal / injection attempts → rejected
    CHECK(!web::resolve("root", "/../etc/passwd"));
    CHECK(!web::resolve("root", "/a/../../b"));
    CHECK(!web::resolve("root", "/%2e%2e/secret"));   // percent-encoded ".."
    CHECK(!web::resolve("root", "/a\\b"));            // backslash
}

static void test_http_parse() {
    {
        const std::string raw = "GET /api/scores?x=1 HTTP/1.1\r\nHost: localhost\r\nX-Test: yes\r\n\r\n";
        auto r = web::parse_request(raw);
        CHECK(r.has_value());
        CHECK(r->method == "GET" && r->target == "/api/scores?x=1" && r->version == "HTTP/1.1");
        CHECK(r->header("host") == "localhost");      // case-insensitive
        CHECK(r->header("X-TEST") == "yes");
        CHECK(r->body.empty());
    }
    {
        const std::string raw = "POST /api/scores HTTP/1.1\r\nContent-Length: 22\r\n\r\n{\"name\":\"AB\",\"score\":7}";
        auto r = web::parse_request(raw);
        CHECK(r.has_value());
        CHECK(r->method == "POST");
        CHECK(r->body == "{\"name\":\"AB\",\"score\":7}");
    }
    CHECK(!web::parse_request("garbage-no-spaces\r\n\r\n").has_value());  // malformed request line
}

static void test_http_serialize() {
    const web::Response r = web::make_response(404, "Not Found", "nope\n");
    const std::string   s = to_str(web::serialize(r));
    CHECK(s.rfind("HTTP/1.1 404 Not Found\r\n", 0) == 0);
    CHECK(s.find("Content-Length: 5\r\n") != std::string::npos);
    CHECK(s.find("Connection: close\r\n") != std::string::npos);
    CHECK(s.substr(s.size() - 5) == "nope\n");
}

static void test_leaderboard() {
    web::Leaderboard b;
    b.add("Alice", 10);
    b.add("Bob", 30);
    b.add("Cara", 20);
    CHECK(b.scores().size() == 3);
    CHECK(b.scores()[0].name == "Bob" && b.scores()[0].value == 30);   // sorted desc
    CHECK(b.scores()[2].name == "Alice");

    // sanitize: quotes/backslash/control chars dropped, length capped, empty→anon
    b.add("e\"vil\\\n\tname", 5);
    const std::string sane = b.scores().back().name;
    CHECK(sane.find('"') == std::string::npos && sane.find('\\') == std::string::npos);
    CHECK(web::sanitize_name("") == "anon");

    const std::string json = b.to_json();
    CHECK(json.front() == '[' && json.back() == ']');
    CHECK(json.find("\"name\":\"Bob\",\"score\":30") != std::string::npos);

    // body parse
    std::string name;
    long        v = 0;
    CHECK(web::parse_score_body("{\"name\":\"ZZ\",\"score\":42}", name, v));
    CHECK(name == "ZZ" && v == 42);
    CHECK(!web::parse_score_body("{\"nope\":1}", name, v));

    // save → load round-trip
    const std::string path = "/tmp/hand_engine_scores_test.json";
    CHECK(b.save(path));
    web::Leaderboard b2;
    CHECK(b2.load(path));
    CHECK(b2.scores().size() == b.scores().size());
    CHECK(b2.scores()[0].name == "Bob" && b2.scores()[0].value == 30);
    std::remove(path.c_str());
}

int main() {
    test_mime();
    test_resolve();
    test_http_parse();
    test_http_serialize();
    test_leaderboard();
    if (g_failures == 0) std::printf("server: all tests passed\n");
    else                 std::printf("server: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
