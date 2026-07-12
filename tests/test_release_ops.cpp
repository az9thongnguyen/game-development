// =============================================================================
//  tests/test_release_ops.cpp  —  publish/promote/rollback ops (CTest, real assets::)
// =============================================================================
//  Exercises the release operations end to end against a temporary asset base: the
//  same code the CLI and the graphical Hub Scene call. This is what makes the
//  interactive hub verifiable — the ops are tested headless; only the keypress→op
//  wiring is manual.
// =============================================================================
#include "engine/release/ops.hpp"

#include "engine/assets.hpp"
#include "engine/release/release.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace engine;

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static void write(const std::string& path, const std::string& text) {
    assets::write_file(path, std::vector<uint8_t>(text.begin(), text.end()));
}

int main() {
    // Isolate to a scratch base so the test never touches the real store.
    assets::set_base_path("test_ops_tmp");
    const std::vector<std::string> known = {"fps"};

    // A shippable project with no assets (package hash = the empty/FNV-basis hash).
    write("p.gameproject", "gameproject1\nname Ops Demo\nschema 1\nentry fps\n");

    // publish → development
    OpResult pub = publish("p.gameproject", "development", "v1", known);
    CHECK(pub.ok);
    CHECK(pub.message.rfind("published", 0) == 0);
    auto dev = current_release("development");
    CHECK(dev.has_value() && *dev == "cbf29ce484222325");   // empty package → FNV offset basis

    // re-publish identical → verified no-op
    OpResult pub2 = publish("p.gameproject", "development", "again", known);
    CHECK(pub2.ok && pub2.message.rfind("verified", 0) == 0);

    // promote development → preview → production
    CHECK(promote("development", "preview", "share").ok);
    CHECK(promote("preview", "production", "ship").ok);
    CHECK(current_release("production").value_or("") == *dev);

    // rollback guards: bad id and bad channel are refused
    CHECK(!rollback("production", "deadbeefdeadbeef", "x").ok);   // no such release
    CHECK(!rollback("../evil", *dev, "x").ok);                    // path-traversal channel name

    // rollback to a real (known-good) release succeeds
    CHECK(rollback("production", *dev, "revert").ok);

    // a project with a missing asset is not shippable
    write("broken.gameproject", "gameproject1\nname Broken\nschema 1\nentry fps\nasset map no/such.map\n");
    OpResult bad = publish("broken.gameproject", "development", "", known);
    CHECK(!bad.ok && bad.message.find("missing asset: no/such.map") != std::string::npos);

    // an unknown channel name is refused before any I/O
    CHECK(!publish("p.gameproject", "bad/name", "", known).ok);

    // the audit log recorded the successful moves (append-only, readable)
    auto log = assets::load_file(audit_log_path());
    CHECK(log.has_value() && !log->empty());

    if (g_failures == 0) std::printf("release_ops: all tests passed\n");
    else                 std::printf("release_ops: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
