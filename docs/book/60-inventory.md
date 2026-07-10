# Chapter 60 — Inventory (Slice #3)

> **What this is.** The third backend service — a per-user **inventory** of item
> quantities the game grants and consumes. Same spine, same shape as cloud save;
> the one idea worth its own chapter is the **server-enforced non-negative
> consume** — the rule that makes an inventory more than a number, and the reason
> you never trust the client to spend. The colony demo grows a tiny economy:
> gather wood, spend it to build. Code: `baas/inventory/*`, SDK
> `client.inventory()`, `src/games/colony/colony_scene.cpp`.

---

## 1. The move, a third time

By now the pattern is muscle memory: a table scoped by `(project_id, user_id)`, a
service with its guards, a controller behind the api-key + JWT filters, an SDK
handle, a test suite that boots the real app.

```sql
CREATE TABLE IF NOT EXISTS inventory (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  item TEXT NOT NULL, qty BIGINT NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, user_id, item));
```

Appended to the idempotent migration, same as `saves` — existing DBs gain it on the
next boot.

## 2. The API

| Method + path | Body → Response |
|---|---|
| `GET /v1/inventory` | → `{items:[{item, qty}]}` |
| `GET /v1/inventory/{item}` | → `{item, qty}` (**qty 0** if never held — a count, not a 404) |
| `POST /v1/inventory/{item}/grant` | `{amount}` → `{item, qty}` |
| `POST /v1/inventory/{item}/consume` | `{amount}` → `{item, qty}`; **409** if `qty < amount` |

Two deliberate shapes: `get` returns **0** for an item you've never held (asking "how
many do I have?" always has an answer), and there are **separate** grant/consume verbs
rather than one signed `adjust`. Separate verbs make the intent explicit and let
`consume` carry the one business rule.

## 3. The rule that matters: non-negative consume

Never let the client decide it can afford something. The server reads the current
quantity and refuses to go negative:

```cpp
// baas/inventory/inv_service.cc
const long long cur = ex.empty() ? 0 : ex[0]["qty"].as<long>();
if (cur < amount)
    return {std::nullopt, Error{409, "insufficient", "not enough " + item}};
const long long qty = cur - amount;   // UPDATE … SET qty = qty …
```

A client that tries to spend 100 wood it doesn't have gets a **409** and its balance
is untouched — proven by the test (`consume 100` after only 5 granted leaves 5). This
is the same trust boundary as the leaderboard's anti-spoof: the authoritative decision
lives on the server, never in the request. `amount` is also bounds-checked
(1 … 1e12) and `item` uses the shared `[A-Za-z0-9_-]`, 1–64 whitelist.

## 4. The SDK handle

```cpp
client.inventory().grant("wood", 5, [&](gbaas::Result<gbaas::Item> r){ … });   // r->qty
client.inventory().consume("wood", 10, [&](gbaas::Result<gbaas::Item> r){       // r.error on 409
    if (!r) status = "not enough wood";
});
client.inventory().get("wood", cb);   // Result<Item>
client.inventory().list(cb);          // Result<std::vector<Item>>
```

Same non-blocking client, same `Result<T>` error shape — a 409 arrives as
`r.error`, which the colony surfaces as "not enough wood".

## 5. Colony: a one-resource economy

The demo gained **Gather +5 wood** (`grant`) and **Build −10 wood** (`consume`), with
the live `wood` count shown in the panel (refreshed on login and after each action).
Spend more than you have and the status line reads *"not enough wood"* — the 409 made
visible. It's a toy economy, but it exercises the full grant/consume/insufficient
loop natively and in the browser.

## 6. Three services in, the platform's rhythm is set

Leaderboard, cloud save, inventory — three L1 services, each a bounded, tested,
documented increment on one spine built once. Remote Config, Analytics, and Live
Events are the same rhythm; the realtime services (L2) are the next real *new* problem
(persistent connections), and the dashboard (L3) is the differentiator's surface.

## 7. Checkpoints

- Why does `consume` enforce non-negativity on the **server**? What could a client do
  if it were enforced only in the SDK?
- `get` on a never-held item returns 0, not 404. Why is that the right choice for an
  inventory (but not for cloud save's `get`)?
- Grant and consume are separate endpoints, not one signed `adjust`. What does that
  buy you?
