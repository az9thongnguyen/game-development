// =============================================================================
//  tests/test_baas_migrations.cc  —  versioned schema engine + audit log
// =============================================================================
//  Pure DB test (no HTTP server): drives the migration engine and the audit log
//  directly against a temp SQLite database — the same run_migrations() every boot
//  calls, and the same audit::record/recent the admin path uses. Verifies the H2
//  persistence claims: migrations apply in order, are recorded, are idempotent, and
//  the audit trail round-trips.
// =============================================================================
#include <cstdio>
#include <string>

#include "baas/admin/audit.h"
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

int main() {
    const std::string db_path = "test_baas_migrations.db";
    baastest::cleanup_db(db_path);

    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);

    // A fresh database applies every migration in order and records each one.
    web::db::run_migrations(db);
    auto applied = web::db::applied_migrations(db);
    CHECK(applied.size() == 2);
    CHECK(applied[0].version == 1 && applied[0].name == "initial schema");
    CHECK(applied[1].version == 2 && applied[1].name == "audit log");
    CHECK(!applied[0].applied_at.empty());

    // Migration 2 really created the table: an insert into it must succeed.
    // (round-trips through the same audit API the admin path uses)
    web::audit::record(42, "admin", "project.create", "Demo Project");
    web::audit::record(0, "operator", "config.rotate", "platform-level action");
    auto proj_rows = web::audit::recent(42, 10);
    CHECK(proj_rows.size() == 1);
    CHECK(proj_rows[0].action == "project.create");
    CHECK(proj_rows[0].detail == "Demo Project");
    CHECK(proj_rows[0].project_id == 42);
    auto platform_rows = web::audit::recent(0, 10);   // project_id 0 → NULL rows
    CHECK(platform_rows.size() == 1);
    CHECK(platform_rows[0].action == "config.rotate");
    CHECK(platform_rows[0].project_id == 0);

    // Running migrations again is a no-op: no duplicate version rows, no error.
    web::db::run_migrations(db);
    CHECK(web::db::applied_migrations(db).size() == 2);
    // ...and the audit rows survived (idempotent migrations do not wipe data).
    CHECK(web::audit::recent(42, 10).size() == 1);

    baastest::cleanup_db(db_path);
    if (g_failures == 0) std::printf("baas_migrations: all tests passed\n");
    else                 std::printf("baas_migrations: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
