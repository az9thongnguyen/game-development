// =============================================================================
//  tests/test_baas_release_metrics.cc  —  measuring a release (H2 exit gate)
// =============================================================================
//  Proves the Horizon 2 exit-gate clause "measures a release": events tagged with a
//  client release id are attributed to that release, and the admin summary can compare
//  one release against another (usage, and errors — the signal you actually watch when
//  you ship). Pure DB — no HTTP server. Also confirms migration 3 (the release column)
//  applied, since summary_by_release depends on it.
// =============================================================================
#include <cstdio>
#include <string>

#include "baas/analytics/analytics_service.h"
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

// Count events for one (release, name) pair in a summary_by_release result.
static long long tally(const std::vector<web::analytics::ReleaseCount>& s,
                       const std::string& release, const std::string& name) {
    for (const auto& r : s)
        if (r.release == release && r.name == name) return r.count;
    return 0;
}

int main() {
    const std::string db_path = "test_baas_release_metrics.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);   // must include migration 3 (the release column)

    const long pid = 1;
    const std::string relA = "a1b2c3d4e5f60718";   // pretend release-store content hashes
    const std::string relB = "0f1e2d3c4b5a6970";

    // Release A: mostly healthy — many sessions, a couple errors.
    for (int i = 0; i < 5; ++i) web::analytics::record(pid, 0, "session_start", "{}", relA);
    for (int i = 0; i < 2; ++i) web::analytics::record(pid, 0, "error", "{}", relA);
    // Release B: fewer sessions, and a spike of errors — the kind of regression you
    // want to SEE attributed to the new release before rolling it back.
    for (int i = 0; i < 3; ++i) web::analytics::record(pid, 0, "session_start", "{}", relB);
    for (int i = 0; i < 9; ++i) web::analytics::record(pid, 0, "error", "{}", relB);
    // An unattributed event (older client, no release) lands under "".
    web::analytics::record(pid, 0, "session_start", "{}");

    const auto by_rel = web::analytics::summary_by_release(pid);

    // Each release is measured independently — this is the exit-gate capability.
    CHECK(tally(by_rel, relA, "session_start") == 5);
    CHECK(tally(by_rel, relA, "error") == 2);
    CHECK(tally(by_rel, relB, "session_start") == 3);
    CHECK(tally(by_rel, relB, "error") == 9);
    CHECK(tally(by_rel, "", "session_start") == 1);   // unattributed bucket

    // The regression is visible: release B has far more errors than release A, which is
    // exactly the comparison "measure a release" exists to enable.
    CHECK(tally(by_rel, relB, "error") > tally(by_rel, relA, "error"));

    // The plain (release-agnostic) summary still works and totals across releases.
    long long total_sessions = 0;
    for (const auto& c : web::analytics::summary(pid))
        if (c.name == "session_start") total_sessions = c.count;
    CHECK(total_sessions == 5 + 3 + 1);

    baastest::cleanup_db(db_path);
    if (g_failures == 0) std::printf("baas_release_metrics: all tests passed\n");
    else                 std::printf("baas_release_metrics: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
