// =============================================================================
//  tests/test_baas_dialect.cc  —  the one place the two backends differ
// =============================================================================
//  `db::lock_clause()` is the entire syntactic difference between SQLite and
//  Postgres in this codebase, and it decides whether money can be spent twice. It
//  is one line, which is exactly why it needs a test: a one-line seam that is wrong
//  fails on the backend nobody runs locally.
// =============================================================================
#include <cstdio>
#include <string>

#include <utility>

#include "baas/db/db.h"
#include "baas/leaderboard/lb_service.h"
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
    const std::string name = "test_baas_dialect.db";
    baastest::cleanup_db(name);
    auto db = web::db::make_db_client(baastest::db_url(name));
    web::db::set_client(db);
    web::db::run_migrations(db);

    const bool pg = web::db::dialect() == web::db::Dialect::Postgres;
    std::printf("  backend: %s, lock clause \"%s\"\n", pg ? "postgres" : "sqlite",
                web::db::lock_clause());

    // The url decides the dialect, and the dialect decides the clause. Both
    // directions, because a seam that always answered one way would pass a test that
    // only ever ran on one backend — which is the situation this whole slice is about.
    if (pg) {
        CHECK(std::string(web::db::lock_clause()) == " FOR UPDATE");
    } else {
        // NOT "FOR UPDATE with no effect": SQLite has no such clause and would answer
        // with a syntax error. Empty is the only correct answer here.
        CHECK(std::string(web::db::lock_clause()).empty());
    }

    // ...and a locking read has to actually RUN on this backend. A clause that is
    // right in a string comparison and wrong in the parser is the failure this file
    // exists to catch, so the query is executed rather than inspected.
    const auto rows = web::db::exec(db,
        std::string("SELECT value FROM scores WHERE leaderboard_id=? AND user_id=?") +
            web::db::lock_clause(),
        1, 1);
    CHECK(rows.empty());   // no scores yet — the point is that it parsed and ran

    // ---- the lock ORDER, which no backend here can demonstrate ----------
    // Two transactions taking the same two rows in opposite orders deadlock on
    // Postgres. There is no Postgres to show it on, so the decision was moved out of
    // `apply_match` into a function whose VALUE a test can read — the same move this
    // project made for the autotile piece and the touch layout.
    for (long a = 1; a <= 6; ++a) {
        for (long b = 1; b <= 6; ++b) {
            const auto ab = web::lb::lock_order(a, b);
            const auto ba = web::lb::lock_order(b, a);
            CHECK(ab == ba);                       // THE property: order-independent
            CHECK(ab.first <= ab.second);
            CHECK((ab.first == a && ab.second == b) || (ab.first == b && ab.second == a));
        }
    }
    // ...and it is not the identity, or "both sides agree" would be free.
    CHECK(web::lb::lock_order(9, 2) == std::make_pair(2L, 9L));

    // An unknown scheme is refused rather than guessed at.
    bool threw = false;
    try { web::db::make_db_client("mysql://nope"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    if (g_failures == 0) std::printf("baas_dialect: all tests passed\n");
    else                 std::printf("baas_dialect: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
