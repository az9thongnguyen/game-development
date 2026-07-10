// =============================================================================
//  tests/test_baas_jwt.cc  —  unit tests for password hashing + JWT (no network)
// =============================================================================
#include <cstdio>
#include <string>

#include <sodium.h>

#include "baas/auth/jwt.h"
#include "baas/auth/password.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

static void test_password() {
    const std::string h = web::pw::hash("s3cret-pw");
    CHECK(h != "s3cret-pw");                          // never stored in plaintext
    CHECK(h.rfind("$argon2", 0) == 0);               // argon2 encoded form
    CHECK(web::pw::verify("s3cret-pw", h));           // correct password
    CHECK(!web::pw::verify("wrong", h));              // wrong password
    // Two hashes of the same password differ (random salt).
    CHECK(web::pw::hash("s3cret-pw") != h);
}

static void test_jwt() {
    const std::string secret = "unit-test-secret";
    const std::string tok    = web::jwt::issue(7, 3, secret, 3600);

    const auto ok = web::jwt::verify(tok, secret);
    CHECK(ok.has_value());
    CHECK(ok && ok->sub == 7 && ok->pid == 3);

    CHECK(!web::jwt::verify(tok, "other-secret"));    // wrong key
    CHECK(!web::jwt::verify(tok + "x", secret));      // tampered signature
    CHECK(!web::jwt::verify("not-a-jwt", secret));    // malformed
    CHECK(!web::jwt::verify(web::jwt::issue(7, 3, secret, -1), secret));  // expired
}

int main() {
    if (sodium_init() < 0) {
        std::printf("FAIL: libsodium init\n");
        return 1;
    }
    test_password();
    test_jwt();
    if (g_failures == 0) std::printf("baas_jwt: all tests passed\n");
    else                 std::printf("baas_jwt: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
