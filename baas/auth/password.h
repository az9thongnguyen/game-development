// =============================================================================
//  baas/auth/password.h  —  password hashing (argon2id via libsodium)
// =============================================================================
//  We never store or compare plaintext. hash() returns libsodium's self-describing
//  encoded string (algorithm + params + salt + digest); verify() is constant-time.
//  Requires sodium_init() to have succeeded once at startup.
// =============================================================================
#pragma once

#include <string>

namespace web::pw {

// argon2id with interactive limits. Throws std::runtime_error on failure (OOM).
std::string hash(const std::string& password);

// Constant-time verification against an encoded hash from hash().
bool verify(const std::string& password, const std::string& encoded_hash);

}  // namespace web::pw
