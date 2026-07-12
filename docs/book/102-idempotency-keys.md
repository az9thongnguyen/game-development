# Chapter 102 — Idempotency Keys: Making a Retry Safe

> Code: `baas/db/db.cc` (migration 4) · `baas/inventory/inv_service.{h,cc}` (`grant` +
> `idem_lookup`/`idem_record`) · `baas/inventory/inv_controller.cc` (the `Idempotency-Key`
> header) · `tests/test_baas_idempotency.cc`

The strategy's economy foundations name "atomic transactions, idempotency" as prerequisites for
anything that touches player currency. This chapter adds idempotency to the operation where its
absence bites hardest: granting items.

## The double-grant bug hiding in a network

`inv::grant` adds an amount to a player's item balance. Consider the ordinary failure that every
mobile client hits: the client sends "grant 10 gold," the server grants it, and the response is
lost to a dropped connection. The client, seeing no reply, retries. Now the player has 20 gold
from one intended purchase. The bug is not in the grant logic — it is in the assumption that a
request arrives exactly once. Over a real network, requests arrive *at least* once.

The fix is an idempotency key: the client generates a unique id for the *intent* ("this one
purchase") and sends it with every retry of that request. The server records which keys it has
already completed and replays the stored result instead of applying the effect again.

## A key, a result, a replay

Migration 4 adds the store — `idempotency_keys(project_id, idem_key, result, …)` with
`UNIQUE(project_id, idem_key)`. `grant` gains an optional trailing `idem_key` (default `""`, so
every existing caller and the `baas_inventory` test are untouched) and does two things around the
existing logic:

```cpp
if (!idem_key.empty())
    if (auto prior = idem_lookup(project_id, idem_key))
        return {Item{item, *prior}, std::nullopt};   // replay — do NOT grant again
... compute the new qty, write the inventory row ...
if (!idem_key.empty()) idem_record(project_id, idem_key, qty);   // remember for retries
```

`idem_record` writes with `ON CONFLICT(project_id, idem_key) DO NOTHING` — portable across SQLite
(≥3.24) and Postgres, and harmless if a racing duplicate arrives: first writer wins. The controller
reads the standard `Idempotency-Key` header, caps it at 64 characters, and passes it through; an
empty header means "no idempotency," and every call applies as before.

**Scoping the key (a bug caught in review).** A first cut stored the client's key under
`(project_id, idem_key)` alone. But `grant` runs per *user* (the JWT sets `user_id`) and per
*item* (the path segment), and the key is *client-supplied* — two players who both retry "request
#1," or one client reusing a request-counter across items, would collide. The second grant would
find the first's row and replay the wrong item's quantity for a user who was never actually
credited: a 200 OK reporting a balance that does not exist. The fix keeps the table as-is but
scopes the stored key to the request identity: `grant` composes `"<user_id>|<item>|<idem_key>"`
before lookup/record. Because `item` is validated to `[A-Za-z0-9_-]` (no `|`) and `user_id` is
numeric, the `<user_id>|<item>|` prefix is unambiguous and the arbitrary client key follows it, so
no two distinct (user, item) requests can ever hash to the same stored key. The regression test
covers exactly this: a different user, and a different item, reusing the same key value each get
their own grant rather than a replay.

There is one honest ceiling, marked in the code: `lookup`-then-`record` has a hair-thin window
where two *concurrent* first uses of the same key could both grant. Single-writer SQLite serializes
`execSqlSync`, so that window is effectively closed today; the upgrade for a multi-writer Postgres
backend is a claim-first `INSERT` before the effect. Naming the ceiling is the point — the current
runtime is single-writer, so the simple version is correct *here*, and the fix is written down for
the day the runtime changes.

## The proof

`tests/test_baas_idempotency.cc` walks the exact retry story:

- grant 10 with key `req-1` → balance 10.
- grant 10 with key `req-1` again (the retry) → balance **still 10**, not 20. Replayed.
- grant 10 with a *different* key `req-2` → balance 20. A new intent applies.
- grant 10 with no key → balance 30, then 40. No idempotency, every call applies.
- a grant with a bad amount is rejected *and its key is not recorded*, so a later valid use of
  that key applies rather than replaying a failure.

That last case is the subtle one: idempotency must remember completed *successes*, not rejected
attempts — otherwise a client could poison a key with one bad request. Because `idem_record` runs
only after validation and the write, a rejected grant leaves the key free. Same key, safe retry,
no double-credit — the property currency operations cannot ship without.
