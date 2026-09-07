// =============================================================================
//  engine/resource/resource.cpp  —  FNV-1a content hashing
// =============================================================================
#include "engine/resource/resource.hpp"

#include <algorithm>
#include <string>

namespace engine {

uint64_t content_hash(const std::vector<uint8_t>& bytes) {
    uint64_t h = 14695981039346656037ULL;        // FNV-1a 64-bit offset basis (0xcbf29ce484222325)
    for (uint8_t b : bytes) {
        h ^= static_cast<uint64_t>(b);
        h *= 1099511628211ULL;                   // FNV-1a 64-bit prime
    }
    return h;
}

std::string hash_hex(uint64_t h) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i) { s[static_cast<size_t>(i)] = d[h & 0xF]; h >>= 4; }
    return s;
}

namespace {
// Sort resources by path so the package fingerprint is independent of declaration order.
void sort_by_path(std::vector<PackagedResource>& r) {
    std::sort(r.begin(), r.end(),
              [](const PackagedResource& a, const PackagedResource& b) { return a.path < b.path; });
}
}  // namespace

namespace {
// Everything the package file says except its own hash — written ONCE, so the id is by
// construction the fingerprint of the text it is appended to. Strip the last line of a
// `package.txt`, hash what is left, and the id comes back; there is no second spelling
// of "canonical" that could drift from the first.
std::string canonical_body(const std::string& name, int schema, const std::string& entry,
                           const std::vector<PackagedResource>& sorted) {
    std::string out;
    out += "package1\n";
    out += "project " + name + "\n";
    out += "schema " + std::to_string(schema) + "\n";
    out += "entry " + entry + "\n";
    // Type is metadata and stays out: `asset data foo.def` and `asset map foo.def`
    // would ship the same bytes at the same path.
    for (const auto& r : sorted) out += "resource " + r.type + " " + r.path + " " + hash_hex(r.hash) + "\n";
    return out;
}
}  // namespace

uint64_t package_hash(const std::string& name, int schema, const std::string& entry,
                      std::vector<PackagedResource> resources) {
    sort_by_path(resources);
    const std::string canon = canonical_body(name, schema, entry, resources);
    return content_hash(std::vector<uint8_t>(canon.begin(), canon.end()));
}

std::string build_package(const std::string& name, int schema, const std::string& entry,
                          std::vector<PackagedResource> resources) {
    sort_by_path(resources);
    const std::string body = canonical_body(name, schema, entry, resources);
    const uint64_t    pkg  = content_hash(std::vector<uint8_t>(body.begin(), body.end()));
    return body + "packagehash " + hash_hex(pkg) + "\n";
}

} // namespace engine
