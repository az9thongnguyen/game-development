// =============================================================================
//  baas/db/db.cc  —  DbClient factory, migration, seed (see db.h)
// =============================================================================
#include "baas/db/db.h"

#include <cctype>
#include <stdexcept>

namespace web::db {
namespace {

DbClientPtr g_client;

// The one and only schema migration for Slice #1. Kept as an embedded string
// (not a runtime .sql file) so there is no startup file-path dependency. When a
// SECOND migration is needed (later slices), replace this with a real versioned
// migration mechanism (a table of applied versions + ordered files). Portable
// SQL: runs on SQLite now and is kept conservative for Postgres later.
constexpr const char* kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS projects (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  public_key TEXT NOT NULL UNIQUE,
  secret_key_hash TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  email TEXT,
  password_hash TEXT,
  display_name TEXT NOT NULL,
  is_guest INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE UNIQUE INDEX IF NOT EXISTS ux_users_email
  ON users(project_id, email) WHERE email IS NOT NULL;
CREATE TABLE IF NOT EXISTS leaderboards (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  key TEXT NOT NULL,
  name TEXT NOT NULL,
  sort TEXT NOT NULL DEFAULT 'desc',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, key)
);
CREATE TABLE IF NOT EXISTS scores (
  id INTEGER PRIMARY KEY,
  leaderboard_id INTEGER NOT NULL REFERENCES leaderboards(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  value BIGINT NOT NULL,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(leaderboard_id, user_id)
);
)SQL";

bool is_blank(const std::string& s) {
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    return true;
}

// Our DDL contains no ';' except statement terminators, so a plain split is safe.
// (Drogon prepares one statement per exec, so we cannot hand it the whole blob.)
void exec_each_statement(const DbClientPtr& db, const std::string& sql) {
    std::string stmt;
    for (char c : sql) {
        if (c == ';') {
            if (!is_blank(stmt)) db->execSqlSync(stmt);
            stmt.clear();
        } else {
            stmt += c;
        }
    }
    if (!is_blank(stmt)) db->execSqlSync(stmt);
}

}  // namespace

DbClientPtr make_db_client(const std::string& url) {
    const std::string sqlite_pfx = "sqlite://";
    const std::string pg_pfx     = "postgres://";
    if (url.rfind(sqlite_pfx, 0) == 0) {
        const std::string path = url.substr(sqlite_pfx.size());
        // SQLite is single-writer; one connection avoids "database is locked".
        return drogon::orm::DbClient::newSqlite3Client("filename=" + path, 1);
    }
    if (url.rfind(pg_pfx, 0) == 0) {
        // Needs a Drogon build with libpq (the Homebrew bottle lacks it).
        return drogon::orm::DbClient::newPgClient(url, 1);
    }
    throw std::runtime_error(
        "unsupported db url (expected sqlite:// or postgres://): " + url);
}

void run_migrations(const DbClientPtr& db) {
    exec_each_statement(db, kSchemaSql);
}

std::string seed(const DbClientPtr& db) {
    const std::string public_key = "pk_demo_colony";
    const auto existing =
        db->execSqlSync("SELECT id FROM projects WHERE public_key=?", public_key);
    if (existing.empty()) {
        // secret_key is for future server-to-server / admin use (unused in Slice #1);
        // stored as a placeholder here and hashed for real when the admin API lands.
        const auto ins = db->execSqlSync(
            "INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
            std::string("Colony Demo"), public_key, std::string("unset"));
        const auto pid = ins.insertId();
        db->execSqlSync(
            "INSERT INTO leaderboards(project_id, key, name, sort) VALUES(?,?,?,?)",
            static_cast<long>(pid), std::string("colony_high"),
            std::string("Colony High Scores"), std::string("desc"));
    }
    return public_key;
}

void        set_client(const DbClientPtr& db) { g_client = db; }
DbClientPtr client() { return g_client; }

}  // namespace web::db
