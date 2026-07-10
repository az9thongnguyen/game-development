// =============================================================================
//  baas/auth/jwt.h  —  HS256 JSON Web Tokens over libsodium's HMAC-SHA256
// =============================================================================
//  A JWT is just an encoding: base64url(header) "." base64url(payload) "."
//  base64url(HMAC-SHA256(secret, header "." payload)). We assemble that envelope
//  ourselves and use libsodium's audited crypto_auth_hmacsha256 for the MAC and
//  sodium_memcmp for constant-time verification — no crypto is hand-rolled, and
//  no extra dependency (jwt-cpp) is needed. Claims: sub=user_id, pid=project_id,
//  iat, exp.
// =============================================================================
#pragma once

#include <optional>
#include <string>

namespace web::jwt {

struct Claims {
    long sub;   // user id
    long pid;   // project id
};

// Issue a signed token valid for ttl_seconds from now.
std::string issue(long user_id, long project_id, const std::string& secret,
                  int ttl_seconds);

// Verify signature + expiry. Returns the claims, or nullopt on any failure
// (malformed, bad signature, expired).
std::optional<Claims> verify(const std::string& token, const std::string& secret);

}  // namespace web::jwt
