// =============================================================================
//  tests/test_baas_liveops.cc  —  reversible, audited LiveOps config change
// =============================================================================
//  Proves the Horizon 2 exit-gate clause "runs a reversible LiveOps change without
//  client redeployment": an operator changes a server-side config value, the client
//  read path (cfg::get, what /v1/config serves) sees the new value with no code
//  change, the transition is recorded in the audit log, and the operator can revert
//  by setting the returned previous value back. Pure DB — no HTTP server needed.
// =============================================================================
#include <cstdio>
#include <string>

#include "baas/admin/audit.h"
#include "baas/db/db.h"
#include "baas/remote_config/config_service.h"
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
    const std::string db_path = "test_baas_liveops.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);

    const long pid = 1;   // a project id; no FK enforcement needed for this unit test

    // First set of a new key: previous is unset, client read returns the new value.
    auto prev0 = web::cfg::set_audited(pid, "max_agents", "50", "admin");
    CHECK(!prev0.has_value());                                   // key was newly created
    CHECK(web::cfg::get(pid, "max_agents").value_or("") == "50");

    // The LiveOps change: bump the tunable server-side. The client read path is the
    // SAME cfg::get — no redeployment — and it now returns the new value.
    auto prev1 = web::cfg::set_audited(pid, "max_agents", "80", "admin");
    CHECK(prev1.value_or("") == "50");                           // returns the revert target
    CHECK(web::cfg::get(pid, "max_agents").value_or("") == "80");

    // The transition is auditable: newest entry is config.set with old→new recorded.
    auto trail = web::audit::recent(pid, 10);
    CHECK(!trail.empty());
    CHECK(trail[0].action == "config.set");
    CHECK(trail[0].detail == "key=max_agents old=50 new=80");

    // Reversible: set the returned previous value back → client sees the old value.
    web::cfg::set_audited(pid, "max_agents", *prev1, "admin");
    CHECK(web::cfg::get(pid, "max_agents").value_or("") == "50");

    // Audited delete returns the old value (so it too is revertible via a set).
    auto removed = web::cfg::remove_audited(pid, "max_agents", "admin");
    CHECK(removed.value_or("") == "50");
    CHECK(!web::cfg::get(pid, "max_agents").has_value());
    auto trail2 = web::audit::recent(pid, 10);
    CHECK(trail2[0].action == "config.delete");
    // deleting a missing key is a quiet no-op (no audit noise, nullopt).
    CHECK(!web::cfg::remove_audited(pid, "no_such_key", "admin").has_value());

    baastest::cleanup_db(db_path);
    if (g_failures == 0) std::printf("baas_liveops: all tests passed\n");
    else                 std::printf("baas_liveops: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
