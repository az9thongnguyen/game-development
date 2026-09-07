// =============================================================================
//  baas/db/db.h  —  database access for the BaaS backend
// =============================================================================
//  Wraps Drogon's DbClient behind a tiny seam: a url→client factory, an
//  idempotent schema migration, a seed, and a process-wide accessor the
//  controllers use.
//
//  TWO BACKENDS, and the difference between them is not a detail (chapter 140).
//  SQLite is single-writer, so the pool is ONE connection and a transaction holds
//  the only handle there is — which silently made every read-then-write in this
//  codebase atomic. Postgres has a real pool, and the moment it does, two requests
//  can both read a balance before either writes one. The lock has to be asked for,
//  and `lock_clause()` is where it is asked.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

namespace web::db {

using drogon::orm::DbClientPtr;

// Which backend the process is talking to. Set by `make_db_client`, read by
// `lock_clause` — the ONE place SQL differs between them, so a query that needs a
// row lock is written once and says so.
enum class Dialect { Sqlite = 0, Postgres };

[[nodiscard]] Dialect dialect();

// The row-lock clause for a SELECT whose result is about to be written back.
//
//   " FOR UPDATE" on Postgres — the reader blocks any other transaction reading the
//                 same row for update until this one commits.
//   ""            on SQLite   — the pool is one connection, so a transaction already
//                 holds the only handle. Emitting FOR UPDATE there is not a no-op,
//                 it is a syntax error: SQLite has no such clause.
//
// A caller that forgets this is not obviously wrong on SQLite and quietly wrong on
// Postgres, which is why `test_baas_purchase` runs two buyers at once and why
// `test_baas_dialect` asserts the clause is empty for exactly one of the two.
[[nodiscard]] const char* lock_clause();

// Build a DbClient from a url:
//   "sqlite://PATH"     — single connection; SQLite is single-writer
//   "postgres://..."    — needs a Drogon build with libpq (the Homebrew bottle has
//                         none; the drogonframework/drogon image does)
// `pool` is the connection count and is IGNORED for SQLite: more than one writer
// there means "database is locked", not more throughput.
// Throws std::runtime_error on an unsupported scheme.
DbClientPtr make_db_client(const std::string& url, int pool = 4);

// Apply every schema migration this DB is behind on, in version order, recording
// each in `schema_migrations`. Idempotent — safe to run every boot, and safe on a
// database created before the versioned engine existed (migration 1 is the original
// CREATE TABLE IF NOT EXISTS schema, so it just re-records as applied).
void run_migrations(const DbClientPtr& db);

// One applied schema migration (a row of schema_migrations). Exposed so tests and
// the backup/restore drill can answer "which schema version is this DB at?".
struct MigrationRecord {
    int         version;
    std::string name;
    std::string applied_at;
};
std::vector<MigrationRecord> applied_migrations(const DbClientPtr& db);

// Insert the demo project + its `colony_high` leaderboard if absent.
// Returns the project's public_key (existing or newly inserted).
std::string seed(const DbClientPtr& db);

// Process-wide accessor for the single application DbClient, set once at startup.
// (A single shared client is the whole app's DB handle; controllers read it here
//  rather than threading it through Drogon's framework-constructed objects.)
void        set_client(const DbClientPtr& db);
DbClientPtr client();

}  // namespace web::db
