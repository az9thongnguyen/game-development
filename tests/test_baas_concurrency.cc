// =============================================================================
//  tests/test_baas_concurrency.cc  —  can the same money be spent twice?
// =============================================================================
//  Every other test in this suite calls the service once and checks the answer.
//  This one calls it from eight threads at the same moment and checks the
//  ARITHMETIC still adds up — which is a different question, and the only one that
//  can fail when a connection pool grows.
//
//  IT HAS NO TEETH ON SQLITE, and that is worth saying out loud rather than
//  discovering later. The SQLite pool is one connection, so a transaction holds the
//  only handle there is and two purchases cannot interleave. On Postgres with a real
//  pool they can, and the `FOR UPDATE` that `db::lock_clause()` adds is the only
//  thing that stops them. Run it against Postgres to make it mean something:
//
//      BAAS_TEST_DB=postgres://... ctest --test-dir build/baas -R baas_concurrency
//
//  What it DOES prove on SQLite is that the transactions added in chapter 140 do not
//  deadlock or lose an update under contention — which is not nothing, because the
//  first version of that change self-deadlocked on this exact backend.
// =============================================================================
#include <atomic>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <drogon/drogon.h>
#include <sodium.h>

#include "baas/asset_registry/asset_service.h"
#include "baas/cloud_save/save_service.h"
#include "baas/db/db.h"
#include "baas/inventory/inv_service.h"
#include "baas/remote_config/config_service.h"
#include "baas/leaderboard/lb_service.h"
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

namespace {

constexpr int kThreads     = 8;
constexpr int kPerThread   = 6;
constexpr long long kPrice = 10;
constexpr long long kFunds = 100;      // exactly ten affordable purchases

long make_user(long project_id, const char* name) {
    return static_cast<long>(web::db::insert_id(web::db::client(),
        "INSERT INTO users(project_id, display_name, is_guest) VALUES(?,?,1)",
        project_id, std::string(name)));
}

}  // namespace

int main() {
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }

    const std::string name = "test_baas_concurrency.db";
    baastest::cleanup_db(name);
    auto db = web::db::make_db_client(baastest::db_url(name));
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pk = web::db::seed(db);
    std::printf("  backend: %s\n",
                web::db::dialect() == web::db::Dialect::Postgres ? "postgres" : "sqlite");
    std::printf("  lock clause: \"%s\"\n", web::db::lock_clause());

    const long pid = web::db::exec(db, "SELECT id FROM projects WHERE public_key=?", pk)[0]["id"]
                         .as<long>();
    const long uid = make_user(pid, "Spender");

    CHECK(web::store::upsert(pid, "sword", "gold", kPrice, "sword", 1, "test"));
    CHECK(web::inv::grant(pid, uid, "gold", kFunds).item.has_value());
    CHECK(web::inv::get(pid, uid, "gold").qty == kFunds);

    // ---- eight buyers, one purse ------------------------------------------
    std::atomic<int> bought{0}, refused{0}, other{0};
    std::vector<std::thread> hands;
    for (int t = 0; t < kThreads; ++t) {
        hands.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                const auto r = web::store::buy(pid, uid, "sword", "");
                if (r.item)                            ++bought;
                else if (r.error && r.error->status == 409) ++refused;
                else                                   ++other;
            }
        });
    }
    for (auto& h : hands) h.join();

    const long long gold  = web::inv::get(pid, uid, "gold").qty;
    const long long swords = web::inv::get(pid, uid, "sword").qty;
    std::printf("  %d bought, %d refused, %d errored; gold %lld, swords %lld\n",
                bought.load(), refused.load(), other.load(), gold, swords);

    // THE assertions. Not "most of them worked" — the ledger has to balance exactly.
    CHECK(other.load() == 0);
    CHECK(bought.load() + refused.load() == kThreads * kPerThread);
    CHECK(bought.load() == kFunds / kPrice);          // ten, not eleven
    CHECK(gold == 0);                                  // ...and not below zero
    CHECK(swords == kFunds / kPrice);
    CHECK(gold + swords * kPrice == kFunds);           // nothing created, nothing lost

    // ---- and the same for a bare grant, which had no transaction at all ----
    // Before chapter 140 this was a read and then a write with nothing holding them
    // together: two grants of 1 could both read the same total and both write it + 1.
    const long gid = make_user(pid, "Collector");
    std::atomic<int> granted{0};
    hands.clear();
    for (int t = 0; t < kThreads; ++t) {
        hands.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i)
                if (web::inv::grant(pid, gid, "shell", 1).item) ++granted;
        });
    }
    for (auto& h : hands) h.join();
    const long long shells = web::inv::get(pid, gid, "shell").qty;
    std::printf("  %d grants of 1 -> %lld shells\n", granted.load(), shells);
    CHECK(granted.load() == kThreads * kPerThread);
    CHECK(shells == kThreads * kPerThread);            // every increment survived

    // ---- consume: the same shape, and the one that could go NEGATIVE ----
    {
        const long cid = make_user(pid, "Eater");
        constexpr long long kStock = 20;
        CHECK(web::inv::grant(pid, cid, "apple", kStock).item.has_value());
        std::atomic<int> ate{0}, hungry{0};
        hands.clear();
        for (int t = 0; t < kThreads; ++t) {
            hands.emplace_back([&] {
                for (int i = 0; i < kPerThread; ++i) {
                    const auto r = web::inv::consume(pid, cid, "apple", 1);
                    if (r.item) ++ate; else ++hungry;
                }
            });
        }
        for (auto& h : hands) h.join();
        const long long left = web::inv::get(pid, cid, "apple").qty;
        std::printf("  %d eaten, %d refused; %lld apples left\n", ate.load(), hungry.load(), left);
        CHECK(ate.load() == kStock);
        CHECK(left == 0);
        CHECK(left >= 0);                                   // the one that used to be possible
        CHECK(ate.load() + hungry.load() == kThreads * kPerThread);
    }

    // ---- a leaderboard keeps the BEST, even when everyone submits at once ----
    // `submit` reads the old score and then writes a new one. Two submissions racing
    // could both read the old value and the higher of the two could lose.
    {
        const long sid = make_user(pid, "Climber");
        const auto board = web::lb::find_board(pid, "colony_high");
        CHECK(board.has_value());
        if (board) {
            std::atomic<int> sent{0};
            hands.clear();
            for (int t = 0; t < kThreads; ++t) {
                hands.emplace_back([&, t] {
                    for (int i = 0; i < kPerThread; ++i) {
                        web::lb::submit(*board, sid, 100L * t + i);
                        ++sent;
                    }
                });
            }
            for (auto& h : hands) h.join();
            const auto mine = web::lb::rank_of(*board, sid);
            const long best = 100L * (kThreads - 1) + (kPerThread - 1);
            std::printf("  %d submissions -> best %ld (expected %ld)\n", sent.load(),
                        mine ? mine->value : -1, best);
            CHECK(mine.has_value());
            CHECK(mine && mine->value == best);
        }
    }

    // ---- the FIRST write, which is the one a row lock cannot protect -------
    //
    // Chapter 140 put a locking read in front of every read-then-write and believed
    // that closed it. `FOR UPDATE` locks a ROW, and the first write to a key has none:
    // both threads find nothing, both INSERT, and the loser gets a unique-constraint
    // violation. Where a transaction catches it that is a request that silently did
    // nothing; where there was no transaction — config, catalog, cloud save, the asset
    // registry, all four of them until chapter 141 — it is an uncaught exception on a
    // Drogon event-loop thread, which is to say the server process.
    //
    // This says nothing on SQLite, whose pool of one serialises the pair. It is the
    // reason this file exists and the reason CI now runs the whole suite twice.
    {
        std::atomic<int> ok{0}, threw{0};
        const auto storm = [&](const char* what, const std::function<bool(int)>& one) {
            ok = 0; threw = 0;
            hands.clear();
            for (int t = 0; t < kThreads; ++t)
                hands.emplace_back([&, t] {
                    for (int i = 0; i < kPerThread; ++i) {
                        try { if (one(t)) ++ok; } catch (const std::exception&) { ++threw; }
                    }
                });
            for (auto& h : hands) h.join();
            std::printf("  %s: %d ok, %d threw\n", what, ok.load(), threw.load());
            CHECK(threw.load() == 0);
            CHECK(ok.load() == kThreads * kPerThread);
        };

        // One key, one sku, one slot, one asset — each written for the FIRST time by
        // eight threads at once. Every call must succeed; none may throw.
        storm("config.set    ", [&](int t) {
            web::cfg::set(pid, "hot_key", "v" + std::to_string(t));
            return true;
        });
        storm("catalog.upsert", [&](int t) {
            return web::store::upsert(pid, "hot_sku", "gold", 10 + t, "sword", 1, "test");
        });
        storm("save.put      ", [&](int t) {
            return web::save::put(pid, uid, "hot_slot", "data" + std::to_string(t), 0)
                .meta.has_value();
        });
        storm("asset.put     ", [&](int t) {
            return web::asset::put(pid, "hot_asset", "map", "bytes" + std::to_string(t), 0)
                .meta.has_value();
        });

        // ...and each landed exactly once, with one value, not eight rows.
        CHECK(web::cfg::get(pid, "hot_key").has_value());
        CHECK(web::store::get(pid, "hot_sku").has_value());
        const auto sv = web::save::get(pid, uid, "hot_slot");
        CHECK(sv.has_value());
        // The version counted every write: 48 puts, 48 versions. A lost update here is
        // not a crash, it is a save that came back older than the one that replaced it.
        CHECK(sv && sv->version == kThreads * kPerThread);
        const auto av = web::asset::get(pid, "hot_asset");
        CHECK(av.has_value());
        CHECK(av && av->version == kThreads * kPerThread);
    }

    if (g_failures == 0) std::printf("baas_concurrency: all tests passed\n");
    else                 std::printf("baas_concurrency: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
