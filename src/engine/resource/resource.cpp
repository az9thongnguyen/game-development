// =============================================================================
//  engine/resource/resource.cpp  —  FNV-1a content hashing
// =============================================================================
#include "engine/resource/resource.hpp"

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

} // namespace engine
