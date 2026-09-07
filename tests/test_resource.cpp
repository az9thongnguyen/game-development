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

    // --- package manifest ---
    // package_hash is independent of declaration order (sorted by path)...
    std::vector<PackagedResource> a = {{"map", "b.map", 111}, {"texture", "a.tex", 222}};
    std::vector<PackagedResource> b = {{"texture", "a.tex", 222}, {"map", "b.map", 111}};
    const auto id = [](const std::vector<PackagedResource>& r) {
        return package_hash("Demo", 1, "fps", r);
    };
    CHECK(id(a) == id(b));
    // ...but changes when a content hash changes...
    std::vector<PackagedResource> c = {{"map", "b.map", 111}, {"texture", "a.tex", 999}};
    CHECK(id(a) != id(c));
    // ...and when a path changes.
    std::vector<PackagedResource> d = {{"map", "z.map", 111}, {"texture", "a.tex", 222}};
    CHECK(id(a) != id(d));

    // ...and when the project's IDENTITY changes, which it did not until chapter 145.
    // Two games that ship exactly the same art and differ only in which scene they
    // launch are two releases, not one.
    CHECK(package_hash("Demo", 1, "fps", a) != package_hash("Other", 1, "fps", a));
    CHECK(package_hash("Demo", 1, "fps", a) != package_hash("Demo", 1, "farm", a));
    CHECK(package_hash("Demo", 1, "fps", a) != package_hash("Demo", 2, "fps", a));

    // The case that found it: a project that ships NOTHING. Both of these used to be
    // the FNV offset basis, so the store refused to publish the second one — the id was
    // the hash of an empty resource list rather than of the release.
    CHECK(package_hash("Iso Farm", 1, "iso", {}) != package_hash("Colony", 1, "colony", {}));
    CHECK(package_hash("Iso Farm", 1, "iso", {}) != 14695981039346656037ULL);

    // build_package: canonical text, resources sorted by path, ending in packagehash.
    const std::string pkg = build_package("Demo", 1, "fps", a);
    CHECK(pkg.rfind("package1\n", 0) == 0);                      // magic first
    CHECK(pkg.find("resource texture a.tex 00000000000000de\n") != std::string::npos);  // 222 == 0xde
    CHECK(pkg.find("resource map b.map 000000000000006f\n") != std::string::npos);      // 111 == 0x6f
    CHECK(pkg.find("a.tex") < pkg.find("b.map"));                // sorted by path
    CHECK(pkg.find("packagehash " + hash_hex(id(a))) != std::string::npos);
    CHECK(build_package("Demo", 1, "fps", a) == build_package("Demo", 1, "fps", b));  // order-independent

    // The id IS the fingerprint of the file it is written into: strip the last line and
    // hash what is left. There is no second definition of "canonical" to drift from.
    {
        const std::size_t last = pkg.rfind("packagehash ");
        CHECK(last != std::string::npos);
        const std::string body = pkg.substr(0, last);
        CHECK(hash_hex(content_hash(std::vector<uint8_t>(body.begin(), body.end()))) ==
              hash_hex(id(a)));
        // ...and the line it drops is the last one, so "strip the last line" is a rule a
        // reader can follow and not a coincidence of this fixture.
        CHECK(pkg.find('\n', last) == pkg.size() - 1);
    }

    if (g_failures == 0) std::printf("resource: all tests passed\n");
    else                 std::printf("resource: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
