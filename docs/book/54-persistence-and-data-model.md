# Chapter 54 — Persistence & the Data Model

> **What this is.** Where the data lives: Drogon's `DbClient`, the four tables, how
> one **migration** creates the schema, how a **seed** bootstraps the demo project,
> and the single rule that makes the platform multi-tenant — *every query is scoped
> by `project_id`*. You'll also see why SQLite is enough for this slice and what
> Postgres would take. Code: `baas/db/db.*`, `baas/main.cc`.

---

## 1. One client, two backends

Drogon's `DbClient` abstracts the database behind one async, parameterized-query
interface. We wrap it in a tiny url→client factory so the backend is chosen by a
string:

```cpp
// baas/db/db.cc
DbClientPtr make_db_client(const std::string& url) {
    if (url.rfind("sqlite://", 0) == 0)
        return DbClient::newSqlite3Client("filename=" + url.substr(9), 1);  // 1 conn: SQLite is single-writer
    if (url.rfind("postgres://", 0) == 0)
        return DbClient::newPgClient(url, 1);        // needs a Drogon built with libpq
    throw std::runtime_error("unsupported db url: " + url);
}
```

**SQLite is the backend for this slice**, for a good reason beyond simplicity: the
Homebrew Drogon bottle is built with SQLite3 but **not** libpq, so `newPgClient` isn't
usable at runtime without building Drogon from source. That's fine — SQLite is a real,
durable, transactional database in a single file; it needs zero infrastructure, which
means the demo runs with one command. Postgres is a documented deploy-time step
(rebuild Drogon with `-DBUILD_POSTGRESQL=ON`), and because we keep the SQL
conservative and parameterized, the same queries port. We use one write connection
because SQLite serializes writers anyway — more connections just contend for the lock.

## 2. The schema

Four tables (`baas/db/migrations`… embedded as `kSchemaSql`):

```sql
projects(id, name, public_key UNIQUE, secret_key_hash, created_at)
users(id, project_id, email, password_hash, display_name, is_guest, created_at)
    UNIQUE(project_id, email) WHERE email IS NOT NULL     -- guests have null email
leaderboards(id, project_id, key, name, sort, created_at)  UNIQUE(project_id, key)
scores(id, leaderboard_id, user_id, value, updated_at)     UNIQUE(leaderboard_id, user_id)
```

Read the constraints as the design:

- **`projects`** is the tenant. `public_key` goes in the game (sent as `X-Api-Key`);
  `secret_key_hash` is for future server-to-server/admin use, stored hashed.
- **`users`** are scoped *per project* — the same email can exist in two projects, so
  the uniqueness is on `(project_id, email)`, and only when email is present (guests
  have none).
- **`leaderboards`** — a project may have many boards, each keyed (`colony_high`) with
  a sort direction.
- **`scores`** — `UNIQUE(leaderboard_id, user_id)` is what makes "one best score per
  player per board" enforceable at the database, not just in code.

## 3. Migration and seed

`run_migrations` executes the embedded DDL, split on `;` (our DDL has no other
semicolons) because Drogon prepares one statement at a time. Every statement is
`CREATE … IF NOT EXISTS`, so running it on every boot is **idempotent**.

> **Why an embedded string, not a `migrations/001.sql` file?** With exactly one
> migration, a file-based migration *runner* is premature machinery, and reading a
> file at startup adds a fragile path dependency. The SQL lives in `db.cc` as one
> string; when a *second* migration is needed (a later slice), that is the moment to
> introduce a real versioned mechanism. Build the machine when you have two of the
> thing it manages, not before.

`seed` bootstraps the demo tenant so the colony game has something to talk to —
idempotently (guarded by an existence check, so seeding twice leaves one project /
one board):

```cpp
std::string seed(const DbClientPtr& db) {
    if (db->execSqlSync("SELECT id FROM projects WHERE public_key=?", "pk_demo_colony").empty()) {
        auto ins = db->execSqlSync("INSERT INTO projects(name,public_key,secret_key_hash) VALUES(?,?,?)",
                                   "Colony Demo", "pk_demo_colony", "unset");
        db->execSqlSync("INSERT INTO leaderboards(project_id,key,name,sort) VALUES(?,?,?,?)",
                        (long)ins.insertId(), "colony_high", "Colony High Scores", "desc");
    }
    return "pk_demo_colony";
}
```

Run it with `baas --db sqlite://baas.db --seed`; it prints the public key the game
embeds.

## 4. The multi-tenancy rule

There is exactly one rule that keeps tenants apart, and it is a **discipline, not a
feature**: *every query filters by `project_id`* (or by a `leaderboard_id`/`user_id`
that already belongs to the project). `ApiKeyFilter` resolves the project once
(Chapter 52); each service query then constrains to it:

```sql
SELECT id FROM leaderboards WHERE project_id=? AND key=?
SELECT id FROM users        WHERE project_id=? AND email=?
```

A single forgotten `project_id` clause is a data-leak across customers — the worst
kind of bug — so Chapter 55's test *deliberately* creates two projects and asserts one
never sees the other's scores.

## 5. Parameterized, always

Every value is bound with `?`, never concatenated into SQL. That closes SQL injection
by construction: a display name of `Robert'); DROP TABLE users;--` is stored as a
harmless string, because it is passed as a *parameter*, not spliced into the query
text. The only things we ever concatenate into SQL are fixed, code-chosen tokens
(an `ASC`/`DESC` keyword, a `>`/`<` operator — Chapter 55), never user input.

## 6. The accessor

Controllers are constructed by Drogon, so we can't hand them the client; instead a
process-wide accessor (set once at startup) hands it out:

```cpp
web::db::set_client(db);        // in main() / in each test
… db::client()->execSqlSync(…)  // anywhere a service needs it
```

A single global for the one application-wide DB handle is the right amount of
structure — not a dependency-injection framework for a value that is set once and
never changes.

## 7. Checkpoints

- Why is `UNIQUE(project_id, email)` (partial, on non-null email) the right
  constraint rather than `UNIQUE(email)`?
- We seed idempotently. Why does that matter for a server that migrates on every boot?
- Where, exactly, would a cross-tenant data leak come from, and what single habit
  prevents it?
