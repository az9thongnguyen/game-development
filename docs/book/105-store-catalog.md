# Chapter 105 — A Store Catalog: The Server Owns the Price

> Code: `baas/db/db.cc` (migration 5) · `baas/store/store_service.{h,cc}` (`Offer`, `get`,
> `list`, `upsert`, `buy`) · `baas/store/store_controller.{h,cc}` · `baas/admin/admin_controller.cc`
> (`defineOffer`) · `tests/test_baas_catalog.cc`

Chapter 103 made a purchase atomic, but the *caller* supplied the price — the client said "spend 30
gold for a sword." That is fine for an internal helper and unacceptable for a real store: a client
that names its own price can pay one gold for a legendary item. This chapter closes that by giving
the server a **catalog** — priced offers the server owns — so the client buys a *SKU*, never a cost.

## An offer is a price and a reward

Migration 5 adds a `catalog` table: a SKU maps to a price (`cost` of a `currency`) and a reward
(`amount` of an `item`), unique per project. `store::Offer` mirrors a row, and the service is small:
`get`/`list` to read, `upsert` to define. `upsert` is an admin action (audited, `catalog.upsert`)
and refuses a non-positive cost or amount — an offer must charge *something* and grant *something*,
or it is not an offer.

The interesting method is `buy`, and its whole job is to *not* reimplement the purchase:

```cpp
inv::Result buy(long project_id, long user_id, const std::string& sku, const std::string& idem_key) {
    const auto offer = get(project_id, sku);
    if (!offer)
        return {std::nullopt, inv::Error{404, "unknown_sku", "no such catalog entry: " + sku}};
    return inv::purchase(project_id, user_id, offer->currency, offer->cost, offer->item,
                         offer->amount, idem_key);
}
```

It resolves the SKU to an offer, then hands the server-defined `currency`, `cost`, `item`, and
`amount` to the already-tested atomic `inv::purchase` from Chapter 103. That reuse is the point: the
catalog does not need its own transaction, its own rollback, or its own idempotency — it inherits all
three by delegating. The only thing `buy` adds is the price lookup and the "unknown SKU" case. This
is the ladder in practice: the atomic, idempotent purchase already existed a chapter ago, so the
store is a thin resolver on top, not a second copy of the hard part.

## Two doors: admin defines, player buys

The catalog has two HTTP faces. Defining offers is an admin action, so it lives in `AdminController`
behind the project secret: `PUT /v1/admin/catalog/{sku}` with `{currency, cost, item, amount}`. The
client face is a new `StoreController`: `GET /v1/store/catalog` needs only an api-key (prices are
public), while `POST /v1/store/buy/{sku}` needs a logged-in user (the JWT), because a purchase spends
and grants against *that* player. The buy endpoint forwards the `Idempotency-Key` header, so a
retried buy is safe end to end — the header flows to `inv::purchase`'s scoped key.

## The proof

`tests/test_baas_catalog.cc` drives the service directly:

- Define `sword_pack` = 30 gold → 1 sword; a zero-cost or bad-SKU offer is rejected.
- Fund the player 100 gold and `buy("sword_pack")` → 1 sword received, 30 gold spent — the amounts
  come from the catalog, not the caller.
- Buy an unknown SKU → 404, nothing spent.
- Buy something unaffordable → 409 insufficient, and because `buy` rides on the atomic purchase, the
  balance is *unchanged* (the rollback is inherited, not re-proven by luck).
- Retry a buy with the same idempotency key → not charged twice.
- Re-price the SKU, then buy again → the *new* price is charged. The server owns the price, and
  changing it is a catalog edit, not a client change — a LiveOps lever (Chapter 99) applied to the
  economy.

The re-pricing case is the one that ties the slice back to the strategy: a store whose prices live
server-side is a store you can run a sale on without shipping a client build. What is still ahead in
economy foundations, named not faked: real-money receipt validation (a platform-integration slice,
not a hand-built one), which arrives when a reference game sells for actual currency rather than the
in-game gold this catalog trades in.
