// =============================================================================
//  baas/auth/password.cc  —  see password.h
// =============================================================================
#include "baas/auth/password.h"

#include <stdexcept>

#include <sodium.h>

namespace web::pw {

std::string hash(const std::string& password) {
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(out, password.c_str(), password.size(),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        throw std::runtime_error("password hashing failed (out of memory?)");
    }
    std::string result(out);   // NUL-terminated encoded string
    sodium_memzero(out, sizeof(out));
    return result;
}

bool verify(const std::string& password, const std::string& encoded_hash) {
    return crypto_pwhash_str_verify(encoded_hash.c_str(), password.c_str(),
                                    password.size()) == 0;
}

}  // namespace web::pw
