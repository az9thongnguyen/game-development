// =============================================================================
//  tests/test_resource.cpp  —  content hashing (dependency-free, CTest)
// =============================================================================
//  Locks the FNV-1a content-hash contract the resource identity / package manifest
//  rely on: deterministic, avalanche on a 1-byte change, known offset-basis vector,
//  and a stable hex rendering.
// =============================================================================
#include "engine/resource/resource.hpp"

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

static std::vector<uint8_t> bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

int main() {
    // Empty input hashes to the FNV-1a 64-bit offset basis (a well-known constant).
    CHECK(content_hash({}) == 14695981039346656037ULL);

    // Deterministic: same bytes -> same hash.
    CHECK(content_hash(bytes("hello world")) == content_hash(bytes("hello world")));

    // Avalanche: a one-byte change flips the hash.
    CHECK(content_hash(bytes("hello world")) != content_hash(bytes("hello worlx")));

    // Order matters (not a commutative checksum).
    CHECK(content_hash(bytes("ab")) != content_hash(bytes("ba")));

    // hex rendering: 16 lowercase hex chars, matching the value.
    const std::string basis = hash_hex(14695981039346656037ULL);
    CHECK(basis.size() == 16);
    CHECK(basis == "cbf29ce484222325");   // 0xcbf29ce484222325 == the offset basis

    if (g_failures == 0) std::printf("resource: all tests passed\n");
    else                 std::printf("resource: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
