# Chapter 98 — Production Persistence: Versioned Migrations and a Failure Drill

> Code: `baas/db/db.{h,cc}` (`run_migrations`, `applied_migrations`, `kMigrations`) ·
> `baas/admin/audit.{h,cc}` (`record`, `recent`) · `baas/admin/admin_service.cc` (the audit
> call site) · `baas/ops/backup_restore_drill.sh` · `tests/test_baas_migrations.cc` ·
> Runbook: `docs/guides/backup-restore-drill.md`

The release-store chapters (93–97) made the *client artifact* trustworthy: immutable,
content-addressed, promotable, rollbackable. This chapter starts the parallel work on the
*server*: making the BaaS database trustworthy enough to operate a real game. That is the
heart of Horizon 2 — "operate a real small game" — and its exit gate names the test bluntly:
the platform must **survive a documented failure drill**. You don't get to claim durable
persistence; you have to demonstrate it.

Two things have to exist before that claim is honest: a schema that can *change* without
hand-editing production, and a backup you have actually *restored from*. This chapter builds
both, plus the first slice of the audit trail that RBAC will later hang off.

## The schema was a single blob

Slice #1 shipped the whole schema as one embedded string that `run_migrations` ran on every
boot. Every statement was `CREATE TABLE IF NOT EXISTS`, so re-running it was a harmless no-op.
That is a fine *first* migration and a terrible *migration system*: the day you need to add a
column or a table, there is nowhere to put "this DB is at version N, apply N+1 onward." The
code even said so, in a comment: *"When a SECOND migration is needed, replace this with a real
versioned migration mechanism."* Horizon 2 is that day.

## A ledger and an ordered list

The mechanism is deliberately small — a table that remembers what has run, and a list of what
*can* run:

```cpp
struct Migration { int version; const char* name; const char* sql; };
constexpr Migration kMigrations[] = {
    {1, "initial schema", kMigration1},   // the old blob, unchanged
    {2, "audit log",      kMigration2Audit},
};
```

`run_migrations` creates a `schema_migrations(version, name, applied_at)` ledger if absent,
reads `MAX(version)` (0 on a fresh or pre-versioning database), and applies exactly the
migrations above that watermark — recording each as it goes:

```cpp
for (const auto& m : kMigrations) {
    if (m.version <= current) continue;        // already applied — skip
    exec_each_statement(db, m.sql);
    db->execSqlSync("INSERT INTO schema_migrations(version, name) VALUES(?,?)",
                    m.version, std::string(m.name));
}
```

Three properties matter, and the test pins all three:

1. **Ordered and recorded.** A fresh DB ends with rows `{1, 2}`.
2. **Idempotent.** Running it again applies nothing, inserts no duplicate rows, and — crucially
   — *loses no data*. Boot-time migration must be safe to run on every start.
3. **Backward-safe.** A database created back in Slice #1 has the tables but no
   `schema_migrations`. On first boot under the new engine the ledger is created, migration 1's
   `IF NOT EXISTS` statements re-record it as applied (touching nothing), and migration 2 runs.
   No manual backfill.

The rule that keeps this honest is the one in the runbook: **never edit or renumber a shipped
migration.** A deployed database has already recorded version 2; if you change what version 2
*means*, that database will never see the change because it skips it. New intent is always a
new, higher version. The append-only migration list is the same discipline as the append-only
audit log and the immutable release store — the whole platform prefers "add a new record" over
"mutate an old one," because mutation is what you can't audit or roll back.

## Migration 2 earns its keep: the audit log

A migration engine with one migration proves nothing, so version 2 does real work — it adds
`audit_log`, the "who changed what, when" table that Horizon 2's RBAC/audit item needs:

```sql
CREATE TABLE IF NOT EXISTS audit_log (
  id INTEGER PRIMARY KEY, project_id INTEGER, actor TEXT NOT NULL,
  action TEXT NOT NULL, detail TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
```

`web::audit::record(project_id, actor, action, detail)` appends a row; `recent(project_id,
limit)` reads them back newest-first. Two design choices are worth naming:

- **`project_id == 0` means platform-level** and is stored as SQL `NULL`, so a genuine project
  with id 0 (there isn't one, but still) is distinct from "no project." `recent(0, …)` queries
  `WHERE project_id IS NULL`.
- **Auditing must never break the action it records.** `record` swallows and logs any DB error
  rather than throwing back into `create_project`. An audit failure should degrade the trail,
  not the operation — the write it decorates already succeeded.

Then exactly one real call site proves the wire is live: `admin::create_project` calls
`audit::record(id, "admin", "project.create", name)` after inserting. The `baas_admin`
integration test still passes, so provisioning now leaves a trail without changing its
behaviour. This is the ponytail line held on purpose — one table, one reader, one call site.
Roles, short-lived credentials, and secret rotation are the *rest* of RBAC and get their own
slices; they are not faked here because the audit table happens to exist.

## The drill: back up, lose everything, restore, prove it

A backup nobody has restored is a rumour. `baas/ops/backup_restore_drill.sh` turns the rumour
into a passing check by doing the scary part on purpose:

1. Capture the source state — `PRAGMA integrity_check` and a SHA-1 of `sqlite3 .dump`.
2. Take a consistent online backup (`sqlite3 SRC ".backup DEST"` — safe with live writers; not
   a raw file copy).
3. **Delete the database** and its journal/WAL files. Total loss, simulated.
4. Restore from the backup.
5. Verify: integrity check passes *and* the restored `.dump` SHA-1 equals the original.

If the two hashes differ, the script exits non-zero. Run against the repo it prints a matching
`6e612f79…` before and after and `DRILL PASSED`. That is the Horizon 2 exit gate satisfied for
the persistence layer's local runtime — not asserted, executed.

## The honest edge: Postgres is wired but not live

The strategy asks for a PostgreSQL production path "while retaining SQLite for local
development." The seam is there — `make_db_client` already branches on `postgres://` and calls
`newPgClient`, the migration SQL is portable, and the drill script has a `pg_dump`/`pg_restore`
branch. What is *missing* is not code: the Homebrew Drogon bottle is built without libpq
(`otool -L` shows only `libsqlite3`), so there is no Postgres backend to connect to, and no
local server is running. Bringing Postgres up is a **deploy-side toolchain step** — rebuild
Drogon with `-DBUILD_POSTGRESQL=ON` — documented in the runbook, not an application change.

This is the pattern the whole book keeps returning to: state what is *verified* (SQLite
migrations and a restored backup, both tested) separately from what is *wired but unproven*
(Postgres at runtime). A migration engine you can trust and a backup you have restored from are
real progress toward operating a game; claiming a Postgres deployment you never booted would
not be. The seam makes the upgrade a build-and-config change; the honesty makes the claim worth
something.
