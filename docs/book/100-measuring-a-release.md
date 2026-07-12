# Chapter 100 — Measuring a Release, and a Migration That Alters a Table

> Code: `baas/db/db.cc` (migration 3) · `baas/analytics/analytics_service.{h,cc}`
> (`record` with `release`, `summary_by_release`) · `baas/analytics/analytics_controller.cc`
> · `tests/test_baas_release_metrics.cc`

This chapter closes the last clause of the Horizon 2 exit gate. The gate reads: the platform
"survives a documented failure drill" (Chapter 98), "measures a release," and "runs a reversible
LiveOps change without client redeployment" (Chapter 99). With this slice, all three are
demonstrated for the local runtime. It also gives the migration engine from Chapter 98 its first
real *alter* — the operation that engine actually exists for.

## Why "measure a release" needs a new column

Analytics already ingested events: a client POSTs `{name, props}` and the server records a row
scoped to the project. The admin summary counts events by name. What it could not do was answer
"how is *this* release doing versus the last one?" — because a row had no idea which release
produced it. You cannot measure a release you cannot identify.

The fix is a first-class dimension on the data: a `release` column holding the client's release
id (the release-store content hash from Chapters 92–93, or any build id the client chooses). Once
events carry it, "measuring a release" is just a `GROUP BY release, name`.

## The migration that earns the engine its keep

Migrations 1 and 2 created tables. A `CREATE TABLE IF NOT EXISTS` is cheap to re-run — it does
nothing the second time — so those migrations would survive even a naive "run the whole schema
every boot" approach. Migration 3 is different:

```sql
ALTER TABLE analytics_events ADD COLUMN release TEXT NOT NULL DEFAULT '';
```

Run that twice and the second run *errors* ("duplicate column"). This is precisely the case the
versioned engine was built for: it records that version 3 has been applied and never runs it
again. A database that was at version 2 gets the column added exactly once; a fresh database gets
all three in order; a database already at version 3 skips it. The `NOT NULL DEFAULT ''`
backfills every existing row with the empty (unattributed) release, so the alter is safe on a
populated table. This is the moment the "append a new migration, never edit a shipped one" rule
stops being theoretical — editing migration 1 to add the column instead would have done nothing
to any database that had already recorded version 1.

Both SQLite and Postgres support `ADD COLUMN ... NOT NULL DEFAULT`, so the portability guarantee
from Chapter 98 holds; no application code changes to move this to the documented Postgres build.

## Measuring, concretely

`record` gains an optional trailing `release` (default `""`, so every existing caller and the
`baas_analytics` HTTP test keep compiling and passing). The controller reads an optional
`"release"` string from the event body, caps it at 64 characters, and stores it through a bound
parameter — client-supplied, so it is capped and never concatenated into SQL. Then:

```cpp
std::vector<ReleaseCount> summary_by_release(long project_id) {
    // SELECT release, name, count(*) ... GROUP BY release, name
    //   ORDER BY release, count DESC, name
}
```

The test tells the story the capability is for. Release A ships with 5 sessions and 2 errors;
release B ships with 3 sessions and **9** errors. `summary_by_release` attributes each count to
its release, and the test asserts the thing an operator would actually look for:

```cpp
CHECK(tally(by_rel, relB, "error") > tally(by_rel, relA, "error"));
```

That inequality is a release being measured: the new release's error spike is visible, attributed,
and comparable to the old one — the signal you watch before deciding to promote or roll back
(which Chapters 93–94 already made a one-command move). Unattributed events from older clients
land in the `""` bucket rather than distorting a real release's numbers, and the plain
release-agnostic `summary` still totals across everything.

## Where Horizon 2's exit gate stands

Three clauses, three chapters, all demonstrated against the local runtime with executed tests and
a run drill:

| Exit-gate clause | Chapter | Evidence |
|------------------|---------|----------|
| survives a documented failure drill | 98 | backup/restore drill, verified byte-equal |
| measures a release | 100 | per-release analytics, regression made visible |
| runs a reversible LiveOps change without client redeployment | 99 | audited config change + revert, tested |

That is the exit gate met for the SQLite runtime. What Horizon 2 still contains beyond the gate —
the rest of RBAC (roles, short-lived credentials, secret rotation), request tracing and SLOs,
LiveOps segmentation and experiments, economy foundations, deployment profiles, and a second
online reference game — remains real, buildable work in `baas/`, taken one tested slice at a time
rather than hand-built in a rush. And the honest edge from Chapter 98 still stands: a
live-PostgreSQL acceptance run belongs in a deploy environment with a Drogon build that includes
libpq, and is not claimed here.
