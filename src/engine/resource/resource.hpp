// =============================================================================
//  engine/resource/resource.hpp  —  content hashing (Horizon 0 resource identity)
// =============================================================================
//  The architecture wants every packaged/derived asset to record a content hash so
//  a release can prove exactly which bytes it shipped and a preview can be compared
//  to what was published. This is the smallest primitive of that: a deterministic,
//  dependency-free 64-bit content hash. Pure — no assets::, no SDL. ponytail: one
//  hash function, not a hashing framework; add algorithms the day one is needed.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

// FNV-1a (64-bit): tiny, fast, deterministic, and identical on native and WASM —
// good enough to fingerprint content and detect drift. NOT a cryptographic hash;
// swap in a stronger one behind this signature if adversarial integrity is needed.
uint64_t content_hash(const std::vector<uint8_t>& bytes);

// Lowercase 16-char hex of a content hash, for manifests, logs, and inspect output.
std::string hash_hex(uint64_t h);

} // namespace engine
