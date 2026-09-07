# 140 — The pool was the lock

There was a comment in `inv_service.cc`, written carefully, and true:

> the SELECT-then-UPDATE affordability check is atomic against other requests ONLY
> because the SQLite pool is size 1, so `newTransaction()` reserves the sole
> connection and no concurrent purchase can read the pre-spend balance. On a
> Postgres build with pool > 1, `Deferred` would reopen this TOCTOU.

It is a good comment. It names the assumption, it names what breaks it, and it
names the fix. This chapter is the slice that was supposed to cash it: a
`FOR UPDATE`, a Postgres build, and a test that proves the two go together.

What actually happened is that the comment was **right about the purchase and
wrong about the file it was written in** — and that the Postgres build it was
warning about does not exist.

---

## Two places where the pool was never the lock

`purchase` opens a transaction, so on SQLite it really does hold the only
connection for its whole read-then-write. `grant` and `consume` did not:

```cpp
auto       db = db::client();
const auto ex = db->execSqlSync("SELECT qty FROM inventory WHERE …");
…
db->execSqlSync("UPDATE inventory SET qty=? WHERE …");
```

A pool of one hands out the connection **for a statement**, not for a sequence of
them. Two threads calling `execSqlSync` twice each interleave on a pool of one
exactly as happily as on a pool of ten. So a grant could lose an update, on the
backend everything actually runs on, today.

Except it turned out to be worse than a lost update. `test_baas_concurrency` runs
eight threads granting `1` of an item nobody owns yet, and against the old code
it does this:

```
libc++abi: terminating due to uncaught exception of type drogon::orm::UniqueViolation:
  UNIQUE constraint failed: inventory.project_id, inventory.user_id, inventory.item
```

Both threads read "no row", both take the INSERT branch, and the second one
violates the unique index. Nothing caught it. `inv::grant` is reachable from
`POST /v1/inventory/grant`, so **two concurrent grants of a player's first item
killed the server process** — not a corrupted balance, a crash. It had been
there since chapter 102.

The fix is the same shape in all three places: a transaction, and a read that
asks for a row lock.

---

## One line of SQL

```cpp
const char* lock_clause() {
    return g_dialect == Dialect::Postgres ? " FOR UPDATE" : "";
}
```

That is the entire syntactic difference between the two backends in this
codebase, and it is the one that decides whether money can be spent twice. Empty
on SQLite is not "the same thing with no effect" — SQLite has no `FOR UPDATE`
and answers with a syntax error, which is why a mutation that inverts this line
is caught by a suite that only ever runs on SQLite.

Everything that reads a row it is about to write now goes through it: the
purchase's affordability check and its item upsert, `grant`, `consume`,
`lb::submit`, and the pair of ratings a match moves. That last one locks its two
rows in a **fixed order** — lower user id first — because two transactions taking
the same pair in opposite orders is a deadlock, and a ladder is precisely where
that pair occurs.

---

## A test with no teeth, said out loud

`test_baas_concurrency` cannot fail on SQLite for the reason the locks exist. The
pool is one connection; a transaction holds it; there is no interleaving to
prevent. The file says so in its header rather than leaving somebody to work it
out later, and it says what would give it teeth:

```
BAAS_TEST_DB=postgres://… ctest --test-dir build/baas -R baas_concurrency
```

What it *does* prove on SQLite is that the new transactions do not deadlock or
lose an update under contention — and it earns that, because the first version
of them deadlocked.

---

## Twenty-five minutes of silence

`lb::submit` computes a rank after it writes. The transaction was still in scope:

```cpp
auto tx = db::client()->newTransaction();
…
return {rank_for_value(board, final_value), …};   // ← asks the pool for a connection
```

On SQLite the transaction holds the only connection in the pool, so
`rank_for_value` waited for a handle that could not be freed until the function
returned. The suite did not fail. It **stopped**, and ctest's default timeout is
1500 seconds, so a self-deadlock costs twenty-five minutes before anything says
the word "failed" — during which a hang and a slow test look identical.

The fix is a pair of braces. The lesson is a `TIMEOUT 120` on every test in the
directory: a deadlock should be a red test in two minutes, not a coffee break.

---

## The Postgres path did not exist

With the locks in and the tests green, the remaining job was to run them against
a real Postgres — the whole reason the plan insisted the adapter and the
`FOR UPDATE` were one slice. `baas/ops/pg-test.sh` does it with Docker: a
`postgres:16-alpine` container, and the build inside `drogonframework/drogon`,
which carries the libpq-enabled Drogon that Homebrew's bottle does not.

It built. Then the very first statement died:

```
ERROR:  syntax error at or near ","
LINE 1: INSERT INTO schema_migrations(version, name) VALUES(?,?)
                                                             ^
```

Drogon does not translate `?` placeholders into `$1`. Every one of the **107**
queries in `baas/` is written with `?`. And below that sit two more: `id INTEGER
PRIMARY KEY` is an auto-increment in SQLite and a plain integer column in
Postgres, and the ten `insertId()` calls need a `RETURNING` clause there.

So `db.h`'s "Postgres is a documented deploy-time build" was a **sentence, not a
capability**. Nothing in three years of this backend had ever executed a
statement against it, and nothing would have until a deploy.

The honest response is not to quietly widen the slice. It is to leave the script
in the repository as a **reproduction**: a command anybody can run that fails
with that exact error, and a `❌` in the verification ledger where a `⚠️` used to
be. CI does not run it yet, deliberately — a red job nobody can fix teaches
nothing, and making it green is a slice with its own name.

---

## A suite that passes once

Parameterising the tests on `BAAS_TEST_DB` needed a `db_url(name)` helper, and
the first version appended `.db`:

```cpp
return "sqlite://" + name + ".db";       // name is already "test_baas_auth.db"
```

Every test then wrote `test_baas_auth.db.db` while `cleanup_db` deleted
`test_baas_auth.db`. The full suite passed — the files were new. The second run
failed in ten places, against databases nothing had ever emptied.

It is a small bug with a sharp edge: **a green suite is not evidence unless it is
green twice.** The run that caught it was the second one, and only because the
mutation harness starts by taking a baseline.

---

## What the mutations found

Fifteen single-token mutations across the dialect seam, the three inventory
paths and the two leaderboard ones. **Fourteen killed**, and the shape of the
kills is the interesting part: several died as BUILD failures or as syntax
errors from SQLite itself rather than as failed assertions. Replacing
`newTransaction()` with `client()` does not compile (a `DbClientPtr` has no
`rollback`), and inverting `lock_clause()` makes SQLite reject every locking
read. A seam this thin fails loudly when it is wrong, which is most of why it is
worth having.

Two are worth naming.

**"grant overwrites instead of adding" needed a second attempt.** The first
pattern matched twice — the same line appears in `grant` and in `purchase`'s item
upsert — and the harness reported it SKIPPED rather than applying it to both. A
skipped mutation reads a lot like a killed one in a summary line, which is why
the harness prints them apart.

**One survived, and it cannot be killed here.** `apply_match` locks its two rows
lowest-id-first so two transactions taking the same pair cannot deadlock.
Replacing that with arrival order passes the entire suite, because SQLite has no
row locks and therefore no deadlock to have. The response was not to write an
assertion that would pass for a third reason: the decision moved out into
`lb::lock_order(a, b)`, a pure function of two numbers whose VALUE a test can
read — `lock_order(a,b) == lock_order(b,a)` for every pair, and it is not the
identity. That kills the mutation on the *function*. The mutation on its USE
inside `apply_match` still survives, and will until there is a Postgres to run
against.

That is the same move this project has now made four times: when a property is
only observable in a place you cannot reach, take the decision out of that place
and check its value where you can.

---

## What is still not true

- **Postgres does not work.** Not "is untested" — does not work, with a
  reproduction in the repo. The row locks added here are therefore correct in
  shape and unexercised in the only situation that needs them.
- **`test_baas_concurrency` proves the absence of a deadlock, not the presence of
  a lock.** On SQLite those are different claims and only the first one is being
  made.
- **The fixed lock ordering in `apply_match` is reasoning, not evidence.** Two
  transactions taking two rows in opposite orders deadlock on Postgres; there is
  no Postgres to demonstrate it on.
- **Other read-then-write paths were not audited exhaustively.** The inventory
  ledger and the leaderboard were, because they hold the things a player would
  notice losing. Cloud saves, the asset registry and the test-run queue were not.
- **`/healthz` is still probed by nothing.** CI builds the backend and runs its
  tests; it does not build the image or curl the endpoint. That half of the OPS
  slice is untouched, as is the OpenAPI document.
