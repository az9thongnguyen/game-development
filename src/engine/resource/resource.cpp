// =============================================================================
//  engine/resource/resource.cpp  —  FNV-1a content hashing
// =============================================================================
#include "engine/resource/resource.hpp"

#include <algorithm>

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

uint64_t package_hash(std::vector<PackagedResource> resources) {
    sort_by_path(resources);
    // Fingerprint the canonical "path hexhash\n" of each resource (sorted). Path gives
    // identity, hash gives content; type is metadata and stays out of the fingerprint.
    std::string canon;
    for (const auto& r : resources) canon += r.path + " " + hash_hex(r.hash) + "\n";
    return content_hash(std::vector<uint8_t>(canon.begin(), canon.end()));
}

std::string build_package(const std::string& name, int schema, const std::string& entry,
                          std::vector<PackagedResource> resources) {
    const uint64_t pkg = package_hash(resources);   // computed before the (in-place) sort below
    sort_by_path(resources);
    std::string out;
    out += "package1\n";
    out += "project " + name + "\n";
    out += "schema " + std::to_string(schema) + "\n";
    out += "entry " + entry + "\n";
    for (const auto& r : resources)
        out += "resource " + r.type + " " + r.path + " " + hash_hex(r.hash) + "\n";
    out += "packagehash " + hash_hex(pkg) + "\n";
    return out;
}

} // namespace engine
