// =============================================================================
//  baas/db/db.cc  —  DbClient factory, migration, seed (see db.h)
// =============================================================================
#include "baas/db/db.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
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
  id AUTOID,
  name TEXT NOT NULL,
  public_key TEXT NOT NULL UNIQUE,
  secret_key_hash TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS users (
  id AUTOID,
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
  id AUTOID,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  key TEXT NOT NULL,
  name TEXT NOT NULL,
  sort TEXT NOT NULL DEFAULT 'desc',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, key)
);
CREATE TABLE IF NOT EXISTS scores (
  id AUTOID,
  leaderboard_id INTEGER NOT NULL REFERENCES leaderboards(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  value BIGINT NOT NULL,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(leaderboard_id, user_id)
);
CREATE TABLE IF NOT EXISTS saves (
  id AUTOID,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  slot TEXT NOT NULL,
  data TEXT NOT NULL,
  version INTEGER NOT NULL DEFAULT 1,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, user_id, slot)
);
CREATE TABLE IF NOT EXISTS inventory (
  id AUTOID,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  item TEXT NOT NULL,
  qty BIGINT NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, user_id, item)
);
CREATE TABLE IF NOT EXISTS config (
  id AUTOID,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  key TEXT NOT NULL,
  value TEXT NOT NULL,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, key)
);
CREATE TABLE IF NOT EXISTS analytics_events (
  id AUTOID,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER,
  name TEXT NOT NULL,
  props TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS live_events (
  id AUTOID,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  key TEXT NOT NULL,
  name TEXT NOT NULL,
  starts_at TEXT NOT NULL,
  ends_at TEXT NOT NULL,
  payload TEXT NOT NULL DEFAULT '{}',
  UNIQUE(project_id, key)
);
CREATE TABLE IF NOT EXISTS replays (
  id AUTOID,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  name TEXT NOT NULL,
  data TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS assets (
  id AUTOID,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  name TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT '',
  data TEXT NOT NULL,
  version INTEGER NOT NULL DEFAULT 1,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, name)
);
CREATE TABLE IF NOT EXISTS testruns (
  id AUTOID,
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
  id AUTOID,
  project_id INTEGER,
  actor TEXT NOT NULL,
  action TEXT NOT NULL,
  detail TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS ix_audit_project ON audit_log(project_id, id);
)SQL";

// Migration 3 — a `release` column on analytics events, so telemetry can be attributed
// to the release that produced it ("measures a release", H2 exit gate). ALTER TABLE ADD
// COLUMN with a NOT NULL default is portable across SQLite and Postgres, and — unlike
// migrations 1 and 2 — it evolves an EXISTING table, which is the real reason a versioned
// migration engine exists (a re-run of a CREATE IF NOT EXISTS is free; a second ALTER is not).
constexpr const char* kMigration3ReleaseCol = R"SQL(
ALTER TABLE analytics_events ADD COLUMN release TEXT NOT NULL DEFAULT '';
)SQL";

// Migration 4 — idempotency keys. A client sends a unique key with a mutating request;
// the server records the key + the result so a retried request replays the stored result
// instead of applying the effect twice (no double-grant on a network retry). Scoped per
// project; `result` holds the grant's resulting quantity to replay.
constexpr const char* kMigration4Idempotency = R"SQL(
CREATE TABLE IF NOT EXISTS idempotency_keys (
  id AUTOID,
  project_id INTEGER NOT NULL,
  idem_key TEXT NOT NULL,
  result BIGINT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, idem_key)
);
)SQL";

// The ordered, append-only migration list. To evolve the schema, append a new entry
// with the next version — never edit or renumber a shipped one. run_migrations applies
// exactly those a given database is behind on.
struct Migration {
    int         version;
    const char* name;
    const char* sql;
};
// Migration 5 — store catalog. A priced offer: a SKU maps to a price (spend `cost` of
// `currency`) and a reward (grant `amount` of `item`). This is the "economy catalog,
// currencies" model — the server owns prices, the client buys a SKU, not an arbitrary cost.
constexpr const char* kMigration5Catalog = R"SQL(
CREATE TABLE IF NOT EXISTS catalog (
  id AUTOID,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  sku TEXT NOT NULL,
  currency TEXT NOT NULL,
  cost BIGINT NOT NULL,
  item TEXT NOT NULL,
  amount BIGINT NOT NULL,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, sku)
);
)SQL";

// Migration 6 — project operators with roles (RBAC foundation). Each operator has a
// name, their own hashed key (per-operator credential, distinct from the single project
// secret), and a role (viewer < admin < owner). This is the multi-operator half of the
// RBAC/audit item — a single project secret cannot express "this teammate may read metrics
// but not rotate secrets."
constexpr const char* kMigration6Operators = R"SQL(
CREATE TABLE IF NOT EXISTS operators (
  id AUTOID,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  name TEXT NOT NULL,
  key_hash TEXT NOT NULL,
  role TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, name)
);
)SQL";

// Migrations 7 and 8 — a device id on a guest account. A guest used to be a NEW user
// every launch, which is fine until something belongs to the player: cloud save then
// lands in a fresh account each time and can never be read back. The client keeps an
// opaque id for the installation and hands it over; the same id returns the same guest.
// Nullable, because every existing guest predates it and NULLs stay distinct under a
// unique index — old rows do not collide with each other.
//
// Split in two because ALTER ... ADD COLUMN is not idempotent, and the invariant below
// says a migration is one statement or all-idempotent. One statement each keeps it,
// with no transaction machinery.
constexpr const char* kMigration7GuestDevice = R"SQL(
ALTER TABLE users ADD COLUMN device_id TEXT;
)SQL";

constexpr const char* kMigration8GuestDeviceIndex = R"SQL(
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_device ON users(project_id, device_id);
)SQL";

// Migration 9 — how a board treats a resubmission. Every board until now kept the
// BETTER value, which is right for a high score and wrong for a rating: an Elo goes
// down, and a board that silently refuses to lower it turns a ladder into a record of
// everybody's best day. `mode` is 'best' (the existing behaviour, and the default, so
// every shipped board keeps working) or 'last' (store what was submitted).
//
// One statement, per the invariant above ALTER: a re-run of a CREATE IF NOT EXISTS is
// free and a second ADD COLUMN is not.
constexpr const char* kMigration9BoardMode = R"SQL(
ALTER TABLE leaderboards ADD COLUMN mode TEXT NOT NULL DEFAULT 'best';
)SQL";

constexpr Migration kMigrations[] = {
    {1, "initial schema", kMigration1},
    {2, "audit log", kMigration2Audit},
    {3, "analytics release column", kMigration3ReleaseCol},
    {4, "idempotency keys", kMigration4Idempotency},
    {5, "store catalog", kMigration5Catalog},
    {6, "operators", kMigration6Operators},
    {7, "guest device id", kMigration7GuestDevice},
    {8, "guest device id index", kMigration8GuestDeviceIndex},
    {9, "leaderboard submit mode", kMigration9BoardMode},
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
            if (!is_blank(stmt)) exec(db, stmt);
            stmt.clear();
        } else {
            stmt += c;
        }
    }
    if (!is_blank(stmt)) exec(db, stmt);
}

}  // namespace

namespace {
Dialect g_dialect = Dialect::Sqlite;
}  // namespace

Dialect dialect() { return g_dialect; }

namespace {

// The two spellings of "a surrogate key that fills itself in", and the one spelling of
// "now, as text, in the format SQLite would have written". `to_char` rather than
// `now()::text`: Postgres's own text form carries microseconds and a zone offset, and
// live_events compares these strings to each other with <= — a wider format sorts wrong
// the moment one row was written by each backend.
constexpr const char* kAutoIdSqlite = "INTEGER PRIMARY KEY";
constexpr const char* kAutoIdPg     = "INTEGER PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY";
constexpr const char* kNowPg = "to_char(now() at time zone 'utc','YYYY-MM-DD HH24:MI:SS')";

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// `w` occurs at `i` as a whole word, so a column named `current_timestamp_at` is not
// half-rewritten into something that does not parse.
bool word_at(const std::string& s, std::size_t i, const char* w) {
    const std::size_t n = std::strlen(w);
    if (s.compare(i, n, w) != 0) return false;
    if (i > 0 && ident_char(s[i - 1])) return false;
    if (i + n < s.size() && ident_char(s[i + n])) return false;
    return true;
}

}  // namespace

std::string portable(const std::string& sql, Dialect d) {
    const bool  pg = d == Dialect::Postgres;
    std::string out;
    out.reserve(sql.size() + 64);
    int param = 0;

    for (std::size_t i = 0; i < sql.size();) {
        const char c = sql[i];

        // A quoted literal or identifier is copied through untouched, doubled quote and
        // all. Everything below is a rewrite of SQL; none of it is a rewrite of data.
        if (c == '\'' || c == '"') {
            const char q = c;
            out += c;
            ++i;
            while (i < sql.size()) {
                out += sql[i];
                if (sql[i] == q) {
                    ++i;
                    if (i < sql.size() && sql[i] == q) { out += sql[i]; ++i; continue; }
                    break;
                }
                ++i;
            }
            continue;
        }

        if (c == '?') {
            if (pg) { out += '$'; out += std::to_string(++param); }
            else      out += '?';
            ++i;
            continue;
        }
        if (word_at(sql, i, "AUTOID")) {
            out += pg ? kAutoIdPg : kAutoIdSqlite;
            i += 6;
            continue;
        }
        if (pg && word_at(sql, i, "CURRENT_TIMESTAMP")) {
            out += kNowPg;
            i += 17;
            continue;
        }
        if (word_at(sql, i, "BYTELEN") && i + 7 < sql.size() && sql[i + 7] == '(') {
            // The only rewrite that has to read its own argument, because the two
            // spellings put it in different places. `length()` counts CHARACTERS in
            // SQLite, so the byte count of a save needs a cast through BLOB; Postgres
            // spells the same question `octet_length` and has no BLOB type at all —
            // the first run against it said so ten times, from three services.
            std::size_t j = i + 8;
            int         depth = 1;
            for (; j < sql.size() && depth > 0; ++j) {
                if (sql[j] == '(') ++depth;
                else if (sql[j] == ')') --depth;
            }
            const std::string inner = sql.substr(i + 8, (j - 1) - (i + 8));
            out += pg ? "octet_length(" + inner + ")"
                      : "length(CAST(" + inner + " AS BLOB))";
            i = j;
            continue;
        }

        out += c;
        ++i;
    }
    return out;
}

const char* lock_clause() {
    // Not a table of two strings behind a flag for its own sake: this is the only
    // syntactic difference between the two backends in the whole codebase, and it is
    // the one that decides whether money can be spent twice.
    return g_dialect == Dialect::Postgres ? " FOR UPDATE" : "";
}

Transaction::Transaction(const DbClientPtr& db) {
    auto p = std::make_shared<std::promise<bool>>();
    done_  = p->get_future();
    tx_    = db->newTransaction([p](bool ok) { p->set_value(ok); });
}

Transaction::~Transaction() {
    const bool wait = !rolled_back_;
    tx_.reset();   // this is what enqueues the COMMIT
    if (!wait || !done_.valid()) return;
    // Bounded, because chapter 140 already spent twenty-five minutes inside a wait that
    // could not finish and reported nothing. A timeout here is a message, not a hang.
    if (done_.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        std::fprintf(stderr, "db::Transaction: commit did not report within 10s\n");
}

void Transaction::rollback() {
    rolled_back_ = true;
    tx_->rollback();
}

DbClientPtr make_db_client(const std::string& url, int pool) {
    const std::string sqlite_pfx = "sqlite://";
    const std::string pg_pfx     = "postgres://";
    if (url.rfind(sqlite_pfx, 0) == 0) {
        const std::string path = url.substr(sqlite_pfx.size());
        // SQLite is single-writer; one connection avoids "database is locked". The
        // `pool` argument is deliberately ignored rather than clamped silently —
        // there is no pool size that makes SQLite concurrent.
        g_dialect = Dialect::Sqlite;
        return drogon::orm::DbClient::newSqlite3Client("filename=" + path, 1);
    }
    if (url.rfind(pg_pfx, 0) == 0) {
        // Needs a Drogon build with libpq. `newPgClient` exists as a symbol even in
        // a build without it and throws at connect time, which is why the failure
        // shows up as a runtime error rather than a link one.
        g_dialect = Dialect::Postgres;
        return drogon::orm::DbClient::newPgClient(url, pool < 1 ? 1 : pool);
    }
    throw std::runtime_error(
        "unsupported db url (expected sqlite:// or postgres://): " + url);
}

void run_migrations(const DbClientPtr& db) {
    // The ledger of applied migrations. Created first so a fresh DB and an old
    // pre-versioning DB both start from "version 0 applied".
    exec(db,
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "  version INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)");

    int current = 0;
    const auto max_row =
        exec(db, "SELECT COALESCE(MAX(version), 0) AS v FROM schema_migrations");
    if (!max_row.empty()) current = max_row[0]["v"].as<int>();

    // ponytail: a migration's statements + its ledger insert are NOT wrapped in one
    // transaction — each execSqlSync autocommits. That is safe for migrations 1-4 because
    // each is either a single statement or made of CREATE ... IF NOT EXISTS blocks, so a
    // crash mid-migration re-applies harmlessly on the next boot (the ledger row was never
    // written). INVARIANT for any new migration: it must be a single statement OR every
    // statement idempotent. If you need a multi-statement NON-idempotent migration, wrap
    // exec_each_statement + the ledger insert in `db->newTransaction()` so it is all-or-nothing.
    for (const auto& m : kMigrations) {
        if (m.version <= current) continue;   // already applied — skip
        exec_each_statement(db, m.sql);
        exec(db, "INSERT INTO schema_migrations(version, name) VALUES(?,?)",
                        m.version, std::string(m.name));
    }
}

std::vector<MigrationRecord> applied_migrations(const DbClientPtr& db) {
    std::vector<MigrationRecord> out;
    const auto rows = exec(db,
        "SELECT version, name, applied_at FROM schema_migrations ORDER BY version ASC");
    for (const auto& r : rows)
        out.push_back({r["version"].as<int>(), r["name"].as<std::string>(),
                       r["applied_at"].as<std::string>()});
    return out;
}

std::string seed(const DbClientPtr& db) {
    const std::string public_key = "pk_demo_colony";
    const auto existing =
        exec(db, "SELECT id FROM projects WHERE public_key=?", public_key);
    if (existing.empty()) {
        // secret_key is for future server-to-server / admin use (unused in Slice #1);
        // stored as a placeholder here and hashed for real when the admin API lands.
        // Demo project's secret key is "sk_demo_colony" (stored hashed). main prints it.
        const auto pid = insert_id(db,
            "INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
            std::string("Colony Demo"), public_key, pw::hash("sk_demo_colony"));
        exec(db,
            "INSERT INTO leaderboards(project_id, key, name, sort) VALUES(?,?,?,?)",
            static_cast<long>(pid), std::string("colony_high"),
            std::string("Colony High Scores"), std::string("desc"));
        // Default remote config + a demo live event (always active) for the colony demo.
        exec(db, "INSERT INTO config(project_id, key, value) VALUES(?,?,?)",
                        static_cast<long>(pid), std::string("motd"), std::string("Welcome to Colony!"));
        exec(db, "INSERT INTO config(project_id, key, value) VALUES(?,?,?)",
                        static_cast<long>(pid), std::string("max_agents"), std::string("50"));
        exec(db,
            "INSERT INTO live_events(project_id, key, name, starts_at, ends_at, payload) VALUES(?,?,?,?,?,?)",
            static_cast<long>(pid), std::string("double_wood"), std::string("Double Wood Weekend"),
            std::string("2000-01-01 00:00:00"), std::string("2999-01-01 00:00:00"),
            std::string("{\"wood_mult\":2}"));
    }

    // ---- the farm demo -------------------------------------------------------
    // A second project, because the farm is a second GAME: its prices, its live
    // event and its save slot must not be able to collide with the colony's. The
    // config value and the event payload are both a `defs` OVERRIDE BLOB in the same
    // text format the game's own `farm/crops.def` uses — opaque to the server, which
    // is the point: an operator changes a price by typing the line they would have
    // typed in the file, and nothing here needs to know what a crop is.
    const std::string farm_key = "pk_demo_farm";
    if (exec(db, "SELECT id FROM projects WHERE public_key=?", farm_key).empty()) {
        const auto pid = insert_id(db,
            "INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
            std::string("Farm Demo"), farm_key, pw::hash("sk_demo_farm"));
        // No leaderboard: the farm does not submit a score, and a seeded row nothing
        // reads is a row someone later mistakes for a feature.
        exec(db, "INSERT INTO config(project_id, key, value) VALUES(?,?,?)",
                        static_cast<long>(pid), std::string("farm_defs"),
                        std::string("crop parsnip sell=40\n"));
        // Seeded NOT active (it started and ended in the past), so the festival is a
        // switch an operator flips in the dashboard rather than a permanent buff.
        exec(db,
            "INSERT INTO live_events(project_id, key, name, starts_at, ends_at, payload) VALUES(?,?,?,?,?,?)",
            static_cast<long>(pid), std::string("harvest_festival"),
            std::string("Harvest Festival"),
            std::string("2000-01-01 00:00:00"), std::string("2000-01-02 00:00:00"),
            std::string("crop parsnip sell=90\ncrop pumpkin sell=180\n"));
    }

    // ---- the creature demo ---------------------------------------------------
    // A third project, and the first with a RATING rather than a score. `mode` is
    // 'last' because an Elo is not a personal best: it has to be able to go down, and
    // a board that keeps the better value would freeze every player at their peak.
    const std::string creatures_key = "pk_demo_creatures";
    if (exec(db, "SELECT id FROM projects WHERE public_key=?", creatures_key).empty()) {
        const auto pid = insert_id(db,
            "INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
            std::string("Creatures Demo"), creatures_key, pw::hash("sk_demo_creatures"));
        exec(db,
            "INSERT INTO leaderboards(project_id, key, name, sort, mode) VALUES(?,?,?,?,?)",
            static_cast<long>(pid), std::string("creature_elo"),
            std::string("Creature Ladder"), std::string("desc"), std::string("last"));
    }

    return public_key;
}

void        set_client(const DbClientPtr& db) { g_client = db; }
DbClientPtr client() { return g_client; }

}  // namespace web::db
