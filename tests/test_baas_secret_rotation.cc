// =============================================================================
//  tests/test_baas_secret_rotation.cc  —  project secret rotation (H2 RBAC)
// =============================================================================
//  Proves the "secret rotation" half of the H2 RBAC/audit item: rotating a project
//  secret mints a new one, immediately invalidates the old one, and records the
//  rotation in the audit log. Pure DB — no HTTP server. (sodium is needed because
//  create_project and rotate_secret hash the secret with argon2id.)
// =============================================================================
#include <cstdio>
#include <string>

#include <sodium.h>

#include "baas/admin/admin_service.h"
#include "baas/admin/audit.h"
#include "baas/auth/password.h"
#include "baas/db/db.h"
#include "tests/baas_test_util.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

// Read a project's current stored secret hash (what SecretKeyFilter verifies against).
static std::string secret_hash(long pid) {
    const auto rows = web::db::client()->execSqlSync(
        "SELECT secret_key_hash FROM projects WHERE id=?", pid);
    return rows.empty() ? std::string() : rows[0]["secret_key_hash"].as<std::string>();
}

int main() {
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    const std::string db_path = "test_baas_secret_rotation.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);

    // A fresh project: its returned secret verifies against the stored hash.
    const auto p = web::admin::create_project("Rotation Test");
    const std::string old_secret = p.secret_key;
    CHECK(web::pw::verify(old_secret, secret_hash(p.id)));   // old secret works before rotation

    // Rotate: a new secret is minted and the stored hash changes.
    const std::string new_secret = web::admin::rotate_secret(p.id);
    CHECK(new_secret != old_secret);
    const std::string hash_after = secret_hash(p.id);

    // The new secret verifies; the old secret no longer does (immediate invalidation —
    // this is exactly the check SecretKeyFilter runs on every operator request).
    CHECK(web::pw::verify(new_secret, hash_after));
    CHECK(!web::pw::verify(old_secret, hash_after));

    // The rotation is on the audit trail.
    const auto trail = web::audit::recent(p.id, 10);
    bool found = false;
    for (const auto& e : trail)
        if (e.action == "secret.rotate") found = true;
    CHECK(found);

    baastest::cleanup_db(db_path);
    if (g_failures == 0) std::printf("baas_secret_rotation: all tests passed\n");
    else                 std::printf("baas_secret_rotation: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
