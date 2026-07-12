# Chapter 103 — Atomic Purchase: Spend and Grant, All or Nothing

> Code: `baas/inventory/inv_service.{h,cc}` (`purchase`) · `tests/test_baas_purchase.cc`
> · Drogon `DbClient::newTransaction` / `Transaction::rollback`

Chapter 102 made a single grant retry-safe. A purchase is harder: it is *two* effects — spend a
currency, grant an item — that must both happen or neither. This chapter builds that with a real
database transaction, completing the "atomic transactions, idempotency" pair the strategy names as
the floor for anything touching player currency.

## Why a purchase cannot be two calls

The tempting implementation is `consume(currency, cost); grant(item, amount);`. It is wrong in two
independent ways. First, if the process dies between the two calls, the player has paid and
received nothing — the worst possible outcome for a purchase. Second, there is no single point that
decides "can they afford it?": the consume could succeed and the grant fail (or vice versa in other
orderings), leaving a half-applied purchase. Correctness here is not "call the two operations
carefully"; it is "make the pair indivisible."

## One transaction, committed or rolled back

`purchase` wraps the whole thing in a Drogon transaction. `newTransaction()` reserves a database
connection and issues `BEGIN`; the returned handle *is* a `DbClient`, so the same `execSqlSync`
runs against it. The key semantic: the transaction **commits when the handle is destroyed**, unless
`rollback()` was called. So the control flow is: do the work and return (commit on scope exit), or
call `rollback()` and return on any failure.

```cpp
auto tx = db::client()->newTransaction();
try {
    long long have = /* SELECT currency qty within tx */;
    if (have < cost) { tx->rollback(); return insufficient; }   // afford check, atomic with the spend
    tx->execSqlSync("UPDATE ... currency = have - cost ...");    // spend
    long long qty = /* upsert item within tx */;                // grant
    if (!scoped_key.empty())
        tx->execSqlSync("INSERT INTO idempotency_keys ... ON CONFLICT DO NOTHING", ...);
    return {Item{item, qty}, std::nullopt};                      // commit on scope exit
} catch (const std::exception&) {
    tx->rollback();                                             // any error → nothing applied
    return internal_error;
}
```

Because a single reserved connection runs the whole `BEGIN…COMMIT`, the affordability check and the
spend are atomic against other requests — no one can drain the balance between the `SELECT` and the
`UPDATE`. The insufficient-funds path rolls back before touching anything, so an unaffordable
purchase is a pure no-op. And the idempotency record from Chapter 102 is written *inside* the same
transaction, so the key and the effects commit together: a retry cannot arrive in the window between
"granted the item" and "recorded the key."

The idempotency key is scoped `"purchase|<user_id>|<item>|<key>"` — the `purchase|` tag keeps it from
ever colliding with a grant's key (Chapter 102) even if a client reuses one id value across both.

## The proof

`tests/test_baas_purchase.cc` exercises the three behaviours that matter:

- **Atomic success:** buy a sword for 30 of 100 gold → gold 70, sword 1. Both effects landed.
- **Rollback on insufficient:** try to buy a castle for 1000 gold → 409 insufficient, gold *still 70*,
  no castle row. The spend was rolled back with the (never-applied) grant — this is the case that
  proves the transaction actually rolls back, not just that the happy path works.
- **Idempotent retry:** buy a shield with key `buy-1` → gold 50, shield 1. Retry with `buy-1` →
  replayed, gold *still 50* (not 30), shield *still 1* (not 2). A different key `buy-2` is a real
  second purchase. And a bad amount is rejected before any spend.

The rollback test is the one that earns its keep: a purchase system that only spends when it can
grant is exactly the property a store cannot ship without, and the only way to know the transaction
boundary is real is to make an operation fail inside it and watch the balance stay put.

## Where this leaves economy foundations

Two of the strategy's economy prerequisites — idempotency (102) and atomic transactions (103) — are
now built and tested on the operation that needs them most. Still ahead in that group, named not
faked: a currency/catalog model (priced items rather than a caller-supplied cost), and receipt
validation for real-money purchases (a platform-integration slice, not a hand-built one). Those come
when a reference game actually sells something; the correctness floor they stand on is in place.
