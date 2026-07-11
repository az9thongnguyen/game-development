// =============================================================================
//  tests/test_release.cpp  —  release-store paths / channels (dependency-free, CTest)
// =============================================================================
//  Locks the immutable-store contract: content-addressed paths are stable, the
//  channel pointer round-trips and fails closed, and the trust-boundary validators
//  reject anything that could escape the store or malform a path.
// =============================================================================
#include "engine/release/release.hpp"

#include <cstdio>
#include <string>

using namespace engine;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

int main() {
    const std::string h = "cbf29ce484222325";   // a well-formed 16-char lowercase hex id

    // --- content-addressed paths are exact and stable ---
    CHECK(release_dir(h) == "releases/cbf29ce484222325");
    CHECK(release_manifest_path(h) == "releases/cbf29ce484222325/package.txt");
    CHECK(channel_path("preview") == "channels/preview");

    // --- channel pointer round-trips ---
    CHECK(serialize_channel(h) == "channel1 cbf29ce484222325\n");
    auto parsed = parse_channel(serialize_channel(h));
    CHECK(parsed.has_value() && *parsed == h);
    CHECK(parse_channel("channel1 cbf29ce484222325").has_value());   // trailing newline optional

    // --- channel parse fails closed ---
    CHECK(!parse_channel("").has_value());                    // empty
    CHECK(!parse_channel("channel1").has_value());            // no hash
    CHECK(!parse_channel("chanX cbf29ce484222325").has_value()); // wrong magic
    CHECK(!parse_channel("channel1 xyz").has_value());        // malformed hash
    CHECK(!parse_channel("channel1 CBF29CE484222325").has_value()); // uppercase rejected

    // --- valid_hash_hex ---
    CHECK(valid_hash_hex(h));
    CHECK(!valid_hash_hex(""));                       // empty
    CHECK(!valid_hash_hex("cbf29ce48422232"));        // 15 chars
    CHECK(!valid_hash_hex("cbf29ce4842223255"));      // 17 chars
    CHECK(!valid_hash_hex("CBF29CE484222325"));       // uppercase
    CHECK(!valid_hash_hex("cbf29ce48422232g"));       // non-hex digit

    // --- valid_channel_name (trust boundary: names become path components) ---
    CHECK(valid_channel_name("preview"));
    CHECK(valid_channel_name("production"));
    CHECK(valid_channel_name("my-chan_1"));
    CHECK(!valid_channel_name(""));                   // empty
    CHECK(!valid_channel_name(".."));                 // parent-dir escape
    CHECK(!valid_channel_name("a/b"));                // path separator
    CHECK(!valid_channel_name("has space"));          // whitespace
    CHECK(!valid_channel_name("a.b"));                // dot (no traversal chars at all)
    CHECK(!valid_channel_name(std::string(65, 'x'))); // over length cap

    if (g_failures == 0) std::printf("release: all tests passed\n");
    else                 std::printf("release: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
