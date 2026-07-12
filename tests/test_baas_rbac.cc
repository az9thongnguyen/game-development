// =============================================================================
//  tests/test_baas_rbac.cc  —  operators, roles, authenticate, authorize (H2 RBAC)
// =============================================================================
//  Proves the RBAC foundation: role ordering (viewer < admin < owner), operator
//  provisioning with a per-operator key, authentication (right key → role, wrong key or
//  unknown name → denied), and the authorize() gate. Pure DB; sodium is needed because
//  operator keys are argon2id-hashed like passwords.
// =============================================================================
#include <cstdio>
#include <string>

#include <sodium.h>

#include "baas/db/db.h"
#include "baas/rbac/rbac.h"
#include "tests/baas_test_util.h"

using web::rbac::Role;

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

int main() {
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    const std::string db_path = "test_baas_rbac.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);

    // --- pure role logic: the privilege ordering the whole system hangs on ---
    CHECK(web::rbac::authorize(Role::Owner, Role::Admin));    // owner clears an admin gate
    CHECK(web::rbac::authorize(Role::Admin, Role::Viewer));   // admin clears a viewer gate
    CHECK(web::rbac::authorize(Role::Viewer, Role::Viewer));  // equal clears
    CHECK(!web::rbac::authorize(Role::Viewer, Role::Admin));  // viewer does NOT clear admin
    CHECK(!web::rbac::authorize(Role::Admin, Role::Owner));   // admin does NOT clear owner
    CHECK(web::rbac::role_from_string("owner") == Role::Owner);
    CHECK(!web::rbac::role_from_string("wizard").has_value());

    const long pid = 1;

    // --- provisioning: mint operators with distinct keys and roles ---
    auto alice_key = web::rbac::create_operator(pid, "alice", Role::Owner, "admin");
    auto bob_key   = web::rbac::create_operator(pid, "bob", Role::Viewer, "admin");
    CHECK(alice_key && bob_key);
    CHECK(*alice_key != *bob_key);

    // A duplicate name is refused (the key is returned once; there is no silent overwrite).
    CHECK(!web::rbac::create_operator(pid, "alice", Role::Admin, "admin").has_value());
    // A bad name is refused before any write.
    CHECK(!web::rbac::create_operator(pid, "bad name!", Role::Admin, "admin").has_value());

    // --- authentication: the key proves identity and yields the role ---
    auto a = web::rbac::authenticate(pid, "alice", *alice_key);
    CHECK(a && a->role == Role::Owner);
    auto b = web::rbac::authenticate(pid, "bob", *bob_key);
    CHECK(b && b->role == Role::Viewer);

    // Wrong key for a real operator → denied. Bob's key must not authenticate alice.
    CHECK(!web::rbac::authenticate(pid, "alice", *bob_key).has_value());
    CHECK(!web::rbac::authenticate(pid, "alice", "opk_wrong").has_value());
    // Unknown operator → denied.
    CHECK(!web::rbac::authenticate(pid, "nobody", *alice_key).has_value());
    // Operators are project-scoped: alice's key does not authenticate in another project.
    CHECK(!web::rbac::authenticate(pid + 1, "alice", *alice_key).has_value());

    // --- a role gate composed from the two: only an admin-or-above may "change config" ---
    auto may_change = [&](const std::string& name, const std::string& key) {
        auto op = web::rbac::authenticate(pid, name, key);
        return op && web::rbac::authorize(op->role, Role::Admin);
    };
    CHECK(may_change("alice", *alice_key));    // owner ≥ admin
    CHECK(!may_change("bob", *bob_key));       // viewer < admin
    CHECK(!may_change("alice", *bob_key));     // wrong key → not authenticated at all

    CHECK(web::rbac::list_operators(pid).size() == 2);

    baastest::cleanup_db(db_path);
    if (g_failures == 0) std::printf("baas_rbac: all tests passed\n");
    else                 std::printf("baas_rbac: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
