// =============================================================================
//  tests/test_baas_idempotency.cc  —  retry-safe inventory grant (H2 correctness)
// =============================================================================
//  Proves the idempotency-key guard on inv::grant: a retried grant carrying the same
//  key replays the first result instead of crediting again, so a client that retries a
//  timed-out request is not double-credited — the core correctness property behind
//  "atomic transactions, idempotency" in the strategy's economy foundations. Pure DB.
// =============================================================================
#include <cstdio>
#include <string>

#include "baas/db/db.h"
#include "baas/inventory/inv_service.h"
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
    const std::string db_path = "test_baas_idempotency.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);   // must include migration 4 (idempotency_keys)

    const long pid = 1, uid = 1;
    const std::string item = "gold";

    // First grant with a key: applies. Balance → 10.
    auto r1 = web::inv::grant(pid, uid, item, 10, "req-1");
    CHECK(r1.item && r1.item->qty == 10);

    // SAME key (a retry of the timed-out request): replays, does NOT add again. Still 10.
    auto r1b = web::inv::grant(pid, uid, item, 10, "req-1");
    CHECK(r1b.item && r1b.item->qty == 10);          // <- the whole point: not 20
    CHECK(web::inv::get(pid, uid, item).qty == 10);  // balance really is 10, not double

    // A DIFFERENT key is a genuinely new grant: applies. Balance → 20.
    auto r2 = web::inv::grant(pid, uid, item, 10, "req-2");
    CHECK(r2.item && r2.item->qty == 20);

    // No key = no idempotency: every call applies. Balance → 30.
    auto r3 = web::inv::grant(pid, uid, item, 10, "");
    CHECK(r3.item && r3.item->qty == 30);
    auto r4 = web::inv::grant(pid, uid, item, 10, "");
    CHECK(r4.item && r4.item->qty == 40);

    // Validation still runs even with a key present (bad amount is rejected, not stored).
    auto bad = web::inv::grant(pid, uid, item, -5, "req-bad");
    CHECK(!bad.item && bad.error && bad.error->code == "invalid_amount");
    // ...and the rejected key did not get recorded, so a later valid use of it applies.
    auto reused = web::inv::grant(pid, uid, item, 5, "req-bad");
    CHECK(reused.item && reused.item->qty == 45);

    baastest::cleanup_db(db_path);
    if (g_failures == 0) std::printf("baas_idempotency: all tests passed\n");
    else                 std::printf("baas_idempotency: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
