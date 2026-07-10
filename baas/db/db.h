// =============================================================================
//  baas/db/db.h  —  database access for the BaaS backend
// =============================================================================
//  Wraps Drogon's DbClient behind a tiny seam: a url→client factory, an
//  idempotent schema migration, a seed, and a process-wide accessor the
//  controllers use. SQLite is the runtime backend for Slice #1 (the Homebrew
//  Drogon bottle lacks libpq); Postgres is a documented deploy-time build.
// =============================================================================
#pragma once

#include <string>

#include <drogon/orm/DbClient.h>

namespace web::db {

using drogon::orm::DbClientPtr;

// Build a DbClient from a url:
//   "sqlite://PATH"     — supported (Slice #1 runtime)
//   "postgres://..."    — needs a Drogon build with libpq (see plan S1.8)
// Throws std::runtime_error on an unsupported scheme.
DbClientPtr make_db_client(const std::string& url);

// Create the schema if absent (idempotent — safe to run every boot).
void run_migrations(const DbClientPtr& db);

// Insert the demo project + its `colony_high` leaderboard if absent.
// Returns the project's public_key (existing or newly inserted).
std::string seed(const DbClientPtr& db);

// Process-wide accessor for the single application DbClient, set once at startup.
// (A single shared client is the whole app's DB handle; controllers read it here
//  rather than threading it through Drogon's framework-constructed objects.)
void        set_client(const DbClientPtr& db);
DbClientPtr client();

}  // namespace web::db
