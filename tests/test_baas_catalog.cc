// =============================================================================
//  tests/test_baas_catalog.cc  —  priced catalog + buy-by-SKU (H2 economy)
// =============================================================================
//  Proves the store catalog: an admin defines a priced offer (SKU → spend cost of a
//  currency, grant an item), and a player buys the SKU — spending and receiving the
//  server-defined amounts, not a client-supplied cost. Reuses the atomic inv::purchase,
//  so buy is all-or-nothing and idempotent. Pure DB, no HTTP server.
// =============================================================================
#include <cstdio>
#include <string>

#include "baas/db/db.h"
#include "baas/inventory/inv_service.h"
#include "baas/store/store_service.h"
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
    const std::string db_path = "test_baas_catalog.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);

    const long pid = 1, uid = 1;

    // An admin defines a priced offer: "sword_pack" costs 30 gold, grants 1 sword.
    CHECK(web::store::upsert(pid, "sword_pack", "gold", 30, "sword", 1, "admin"));
    auto offer = web::store::get(pid, "sword_pack");
    CHECK(offer && offer->cost == 30 && offer->amount == 1 && offer->item == "sword");

    // An offer must charge and grant something (a free or empty offer is rejected).
    CHECK(!web::store::upsert(pid, "freebie", "gold", 0, "sword", 1, "admin"));
    CHECK(!web::store::upsert(pid, "bad/sku", "gold", 5, "sword", 1, "admin"));

    // Fund the player and buy by SKU — spend/grant come from the catalog, not the caller.
    web::inv::grant(pid, uid, "gold", 100);
    auto bought = web::store::buy(pid, uid, "sword_pack");
    CHECK(bought.item && bought.item->qty == 1);              // received 1 sword
    CHECK(web::inv::get(pid, uid, "gold").qty == 70);         // paid 30 gold
    CHECK(web::inv::get(pid, uid, "sword").qty == 1);

    // Buying an unknown SKU is a 404, and nothing is spent.
    auto missing = web::store::buy(pid, uid, "no_such_sku");
    CHECK(!missing.item && missing.error && missing.error->code == "unknown_sku");
    CHECK(web::inv::get(pid, uid, "gold").qty == 70);

    // Can't afford it → the underlying atomic purchase rolls back (nothing changes).
    web::store::upsert(pid, "castle", "gold", 1000, "castle", 1, "admin");
    auto broke = web::store::buy(pid, uid, "castle");
    CHECK(!broke.item && broke.error && broke.error->code == "insufficient");
    CHECK(web::inv::get(pid, uid, "gold").qty == 70);         // untouched
    CHECK(web::inv::get(pid, uid, "castle").qty == 0);

    // Idempotent buy: a retry with the same key does not charge twice.
    auto b1 = web::store::buy(pid, uid, "sword_pack", "buy-1");
    CHECK(b1.item && web::inv::get(pid, uid, "gold").qty == 40);   // 70 - 30
    auto b1_retry = web::store::buy(pid, uid, "sword_pack", "buy-1");
    CHECK(b1_retry.item && web::inv::get(pid, uid, "gold").qty == 40);   // NOT charged again

    // A re-priced offer is what the next buy uses (server owns the price).
    CHECK(web::store::upsert(pid, "sword_pack", "gold", 10, "sword", 2, "admin"));
    auto cheap = web::store::buy(pid, uid, "sword_pack", "buy-2");
    CHECK(cheap.item && web::inv::get(pid, uid, "gold").qty == 30);   // 40 - 10 (new price)
    CHECK(web::inv::get(pid, uid, "sword").qty == 1 + 1 + 2);         // first + b1 + this(2)

    baastest::cleanup_db(db_path);
    if (g_failures == 0) std::printf("baas_catalog: all tests passed\n");
    else                 std::printf("baas_catalog: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
