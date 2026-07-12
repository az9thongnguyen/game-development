// =============================================================================
//  tests/test_baas_purchase.cc  —  atomic spend+grant (H2 economy foundations)
// =============================================================================
//  Proves inv::purchase is all-or-nothing: it spends a currency and grants an item in
//  one transaction, an unaffordable purchase changes nothing (rollback), and a retry
//  with the same idempotency key does not purchase twice. This is the "atomic
//  transactions" half of the strategy's economy foundations. Pure DB, no HTTP server.
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
    const std::string db_path = "test_baas_purchase.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);

    const long pid = 1, uid = 1;

    // Give the player 100 gold to spend.
    web::inv::grant(pid, uid, "gold", 100);
    CHECK(web::inv::get(pid, uid, "gold").qty == 100);

    // A purchase spends the currency AND grants the item, atomically.
    auto p1 = web::inv::purchase(pid, uid, "gold", 30, "sword", 1);
    CHECK(p1.item && p1.item->qty == 1);                       // got 1 sword
    CHECK(web::inv::get(pid, uid, "gold").qty == 70);          // spent 30 gold
    CHECK(web::inv::get(pid, uid, "sword").qty == 1);

    // An unaffordable purchase changes NOTHING — the spend is rolled back with the grant.
    auto broke = web::inv::purchase(pid, uid, "gold", 1000, "castle", 1);
    CHECK(!broke.item && broke.error && broke.error->code == "insufficient");
    CHECK(web::inv::get(pid, uid, "gold").qty == 70);          // gold untouched
    CHECK(web::inv::get(pid, uid, "castle").qty == 0);         // no castle granted

    // Idempotent retry: same key must not spend/grant twice.
    auto b1 = web::inv::purchase(pid, uid, "gold", 20, "shield", 1, "buy-1");
    CHECK(b1.item && b1.item->qty == 1);
    CHECK(web::inv::get(pid, uid, "gold").qty == 50);          // 70 - 20
    auto b1_retry = web::inv::purchase(pid, uid, "gold", 20, "shield", 1, "buy-1");
    CHECK(b1_retry.item && b1_retry.item->qty == 1);           // replayed: still 1 shield, not 2
    CHECK(web::inv::get(pid, uid, "gold").qty == 50);          // NOT spent again (would be 30)
    CHECK(web::inv::get(pid, uid, "shield").qty == 1);

    // A different key is a genuinely new purchase.
    auto b2 = web::inv::purchase(pid, uid, "gold", 20, "shield", 1, "buy-2");
    CHECK(b2.item && b2.item->qty == 2);
    CHECK(web::inv::get(pid, uid, "gold").qty == 30);          // 50 - 20

    // Validation runs before anything is spent (bad amount rejected, no state change).
    auto bad = web::inv::purchase(pid, uid, "gold", 10, "potion", -1);
    CHECK(!bad.item && bad.error && bad.error->code == "invalid_amount");
    CHECK(web::inv::get(pid, uid, "gold").qty == 30);          // untouched

    baastest::cleanup_db(db_path);
    if (g_failures == 0) std::printf("baas_purchase: all tests passed\n");
    else                 std::printf("baas_purchase: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
