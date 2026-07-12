// =============================================================================
//  baas/db/db.cc  —  DbClient factory, migration, seed (see db.h)
// =============================================================================
#include "baas/db/db.h"

#include <cctype>
#include <stdexcept>

#include "baas/auth/password.h"

namespace web::db {
namespace {

DbClientPtr g_client;

// Migration 1 — the original Slice-#1 schema. Kept as an embedded string (not a
// runtime .sql file) so there is no startup file-path dependency. Every statement is
// CREATE ... IF NOT EXISTS, so re-running it on a pre-versioning database is a no-op
// (that is what lets an old DB adopt the versioned engine cleanly). Portable SQL:
// runs on SQLite now and stays conservative for the documented Postgres build.
constexpr const char* kMigration1 = R"SQL(
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
CREATE TABLE IF NOT EXISTS saves (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  slot TEXT NOT NULL,
  data TEXT NOT NULL,
  version INTEGER NOT NULL DEFAULT 1,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, user_id, slot)
);
CREATE TABLE IF NOT EXISTS inventory (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  item TEXT NOT NULL,
  qty BIGINT NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, user_id, item)
);
CREATE TABLE IF NOT EXISTS config (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  key TEXT NOT NULL,
  value TEXT NOT NULL,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, key)
);
CREATE TABLE IF NOT EXISTS analytics_events (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER,
  name TEXT NOT NULL,
  props TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS live_events (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  key TEXT NOT NULL,
  name TEXT NOT NULL,
  starts_at TEXT NOT NULL,
  ends_at TEXT NOT NULL,
  payload TEXT NOT NULL DEFAULT '{}',
  UNIQUE(project_id, key)
);
CREATE TABLE IF NOT EXISTS replays (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  name TEXT NOT NULL,
  data TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS assets (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  name TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT '',
  data TEXT NOT NULL,
  version INTEGER NOT NULL DEFAULT 1,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, name)
);
CREATE TABLE IF NOT EXISTS testruns (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  scenario TEXT NOT NULL,
  params TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'pending',
  result TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
)SQL";

// Migration 2 — audit log (H2 RBAC/audit foundation). An append-only record of
// mutating admin/operator actions, so "who changed what, when" is answerable after
// the fact. project_id is nullable for platform-level actions with no single subject.
constexpr const char* kMigration2Audit = R"SQL(
CREATE TABLE IF NOT EXISTS audit_log (
  id INTEGER PRIMARY KEY,
  project_id INTEGER,
  actor TEXT NOT NULL,
  action TEXT NOT NULL,
  detail TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS ix_audit_project ON audit_log(project_id, id);
)SQL";

// The ordered, append-only migration list. To evolve the schema, append a new entry
// with the next version — never edit or renumber a shipped one. run_migrations applies
// exactly those a given database is behind on.
struct Migration {
    int         version;
    const char* name;
    const char* sql;
};
constexpr Migration kMigrations[] = {
    {1, "initial schema", kMigration1},
    {2, "audit log", kMigration2Audit},
};

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
    // The ledger of applied migrations. Created first so a fresh DB and an old
    // pre-versioning DB both start from "version 0 applied".
    db->execSqlSync(
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "  version INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)");

    int current = 0;
    const auto max_row =
        db->execSqlSync("SELECT COALESCE(MAX(version), 0) AS v FROM schema_migrations");
    if (!max_row.empty()) current = max_row[0]["v"].as<int>();

    for (const auto& m : kMigrations) {
        if (m.version <= current) continue;   // already applied — skip
        exec_each_statement(db, m.sql);
        db->execSqlSync("INSERT INTO schema_migrations(version, name) VALUES(?,?)",
                        m.version, std::string(m.name));
    }
}

std::vector<MigrationRecord> applied_migrations(const DbClientPtr& db) {
    std::vector<MigrationRecord> out;
    const auto rows = db->execSqlSync(
        "SELECT version, name, applied_at FROM schema_migrations ORDER BY version ASC");
    for (const auto& r : rows)
        out.push_back({r["version"].as<int>(), r["name"].as<std::string>(),
                       r["applied_at"].as<std::string>()});
    return out;
}

std::string seed(const DbClientPtr& db) {
    const std::string public_key = "pk_demo_colony";
    const auto existing =
        db->execSqlSync("SELECT id FROM projects WHERE public_key=?", public_key);
    if (existing.empty()) {
        // secret_key is for future server-to-server / admin use (unused in Slice #1);
        // stored as a placeholder here and hashed for real when the admin API lands.
        // Demo project's secret key is "sk_demo_colony" (stored hashed). main prints it.
        const auto ins = db->execSqlSync(
            "INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
            std::string("Colony Demo"), public_key, pw::hash("sk_demo_colony"));
        const auto pid = ins.insertId();
        db->execSqlSync(
            "INSERT INTO leaderboards(project_id, key, name, sort) VALUES(?,?,?,?)",
            static_cast<long>(pid), std::string("colony_high"),
            std::string("Colony High Scores"), std::string("desc"));
        // Default remote config + a demo live event (always active) for the colony demo.
        db->execSqlSync("INSERT INTO config(project_id, key, value) VALUES(?,?,?)",
                        static_cast<long>(pid), std::string("motd"), std::string("Welcome to Colony!"));
        db->execSqlSync("INSERT INTO config(project_id, key, value) VALUES(?,?,?)",
                        static_cast<long>(pid), std::string("max_agents"), std::string("50"));
        db->execSqlSync(
            "INSERT INTO live_events(project_id, key, name, starts_at, ends_at, payload) VALUES(?,?,?,?,?,?)",
            static_cast<long>(pid), std::string("double_wood"), std::string("Double Wood Weekend"),
            std::string("2000-01-01 00:00:00"), std::string("2999-01-01 00:00:00"),
            std::string("{\"wood_mult\":2}"));
    }
    return public_key;
}

void        set_client(const DbClientPtr& db) { g_client = db; }
DbClientPtr client() { return g_client; }

}  // namespace web::db
