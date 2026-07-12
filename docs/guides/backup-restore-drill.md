# Backup / Restore Drill & Production Persistence (Horizon 2)

This is the operator runbook for the BaaS persistence layer: how the schema evolves
(versioned migrations), how to prove you can survive data loss (the backup/restore
drill — the Horizon 2 exit-gate failure drill), and the honest state of the
PostgreSQL production path.

## 1. Versioned schema migrations

The schema is no longer a single embedded blob. `run_migrations()`
(`baas/db/db.cc`) maintains a `schema_migrations(version, name, applied_at)` ledger
and applies only the migrations a given database is behind on, in order:

| version | name           | adds                                             |
|--------:|----------------|--------------------------------------------------|
| 1       | initial schema | projects, users, leaderboards, saves, … (Slice 1)|
| 2       | audit log      | `audit_log` + index (H2 RBAC/audit foundation)   |

Properties that the test (`tests/test_baas_migrations.cc`, `ctest -R baas_migrations`)
locks in:

- **Ordered & recorded** — a fresh DB ends with rows `{1, 2}` in `schema_migrations`.
- **Idempotent** — running it again is a no-op; no duplicate rows, no error, no data loss.
- **Backward-safe** — a database created before the versioned engine had no
  `schema_migrations` table; on first boot it is created, migration 1 (all
  `CREATE TABLE IF NOT EXISTS`) re-records harmlessly, and migration 2 applies.

**To evolve the schema:** append a new `{version, name, sql}` entry to `kMigrations`
in `baas/db/db.cc` with the next version number. Never edit or renumber a shipped
migration — a deployed database has already recorded it and will skip it.

**Which version is a DB at?** `SELECT MAX(version) FROM schema_migrations;`

## 2. The backup / restore drill

`baas/ops/backup_restore_drill.sh` performs a full **backup → simulate total loss →
restore → verify** cycle and exits non-zero if the restored database is not
byte-for-byte equivalent to the original.

```sh
# Self-contained demo (seeds its own DB):
baas/ops/backup_restore_drill.sh

# Drill your real database:
baas/ops/backup_restore_drill.sh sqlite://baas.db
```

Verified run (SQLite runtime, this repo):

```
== 1. capture pre-drill state ==
   projects rows: 2   dump sha1: 6e612f7970711c8e826d606fe5bc397ae0fecf3c
== 2. take a consistent online backup ==
== 3. simulate total data loss ==
   source destroyed.
== 4. restore from backup ==
== 5. verify restored == original ==
   restored dump sha1: 6e612f7970711c8e826d606fe5bc397ae0fecf3c
DRILL PASSED: sqlite backup/restore is byte-for-byte equivalent and integrity-checked.
```

The verification is a SHA-1 of `sqlite3 .dump` before and after plus
`PRAGMA integrity_check` — the restore is proven equivalent, not merely present.

**Backup mechanism:** `sqlite3 SRC ".backup DEST"` takes a consistent snapshot even
with live writers (it is not a raw file copy), so it is safe to run against a serving
database. Schedule it (cron/launchd) and copy `DEST` off-box to satisfy the
"restore-point age" operation metric in the strategy scorecard.

## 3. PostgreSQL production path — honest status

The strategy (§7.4) calls for "production persistence path with schema migrations,
PostgreSQL, backups, and restore drills while retaining SQLite for local development."
Status of each piece in this repository:

| Piece | State |
|-------|-------|
| SQLite local dev | **Working & tested** — the default runtime. |
| Versioned migrations | **Working & tested** — portable SQL, runs on both engines. |
| Backup/restore drill (SQLite) | **Verified** — see §2. |
| `postgres://` client wiring | **Present** — `make_db_client` calls `newPgClient`. |
| PostgreSQL at runtime | **Blocked on toolchain** — see below. |
| Backup/restore drill (Postgres) | **Documented** — `pg_dump`/`pg_restore`; the script runs it when a server is reachable. |

**Why Postgres is not live here:** the Homebrew Drogon bottle is built **without
libpq** (`otool -L libdrogon.dylib` shows only `libsqlite3`), so `newPgClient` has no
backend to talk to. Enabling it requires building Drogon from source with PostgreSQL
support:

```sh
# One-time, deploy-side. Requires libpq (brew install libpq) on the PATH.
git clone https://github.com/drogonframework/drogon && cd drogon
cmake -B build -DBUILD_POSTGRESQL=ON -DBUILD_SQLITE=ON -DBUILD_MYSQL=OFF
cmake --build build -j && sudo cmake --install build
# then rebuild baas; set db_url in the config to postgres://…
```

Because migration SQL is portable and the drill script already has a `postgres://`
branch, **no application code changes** are needed to move to Postgres — only the
Drogon build and the `db_url`. That is the parity guarantee; a live-Postgres
acceptance run belongs in a deploy environment with that Drogon build, and is not
claimed here.

**Postgres backup recipe (for that environment):**

```sh
pg_dump   --format=custom --file=baas.dump  postgres://user:pw@host/db     # backup
pg_restore --clean --if-exists --dbname=postgres://user:pw@host/db baas.dump # restore
```

## 4. Where this sits on the roadmap

This closes the **persistence** half of the Horizon 2 exit gate ("survives a
documented failure drill") for the local runtime and delivers the audit-log
foundation of the RBAC/audit item. Still open in H2: roles + short-lived credentials
+ secret rotation (the rest of RBAC), full telemetry (traces/correlation IDs),
LiveOps segmentation/experiments, economy foundations, deployment profiles, and a
second online reference game. See `docs/superpowers/plans/2026-07-11-horizon-0-sequence.md`.
