# Game BaaS — Slice #3: Inventory (design + plan)

- **Date:** 2026-07-10 · **Status:** Approved (autonomous) · **Builds on:** Slices #1–#2.

## 1. Goal

Third L1 service: a per-user **inventory** — item quantities the game grants and
consumes (currency, resources, unlockables). Same spine as before (gateway, auth,
tenancy, DB, SDK); adds one table, one service, one controller, one SDK handle. The
colony demo gathers "wood" and spends it to "build", showing grant/consume and the
insufficient-funds path.

## 2. Data model (append to the idempotent migration)

```sql
CREATE TABLE IF NOT EXISTS inventory (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  item TEXT NOT NULL,
  qty BIGINT NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, user_id, item));
```

## 3. API (api-key + JWT; user from the token; scoped by project+user)

| Method + path | Body → Response |
|---|---|
| `GET /v1/inventory` | → `{items:[{item, qty}]}` |
| `GET /v1/inventory/{item}` | → `{item, qty}` (qty 0 if never held — a count, not a 404) |
| `POST /v1/inventory/{item}/grant` | `{amount}` → `{item, qty}` (qty += amount) |
| `POST /v1/inventory/{item}/consume` | `{amount}` → `{item, qty}`; **409** if `qty < amount` (insufficient) |

`item` uses the same whitelist as save slots (`[A-Za-z0-9_-]`, 1–64). `amount` must
be a positive integer ≤ 1e12. `consume` never lets qty go negative — that's the one
business rule that makes inventory more than a counter, and it's enforced
server-side (a client can't over-spend).

## 4. SDK — `client.inventory()`

```cpp
struct Item { std::string item; long long qty; };
client.inventory().grant(item, amount, cb);    // Result<Item>
client.inventory().consume(item, amount, cb);  // Result<Item> (error on 409)
client.inventory().get(item, cb);              // Result<Item>
client.inventory().list(cb);                   // Result<std::vector<Item>>
```

## 5. Colony integration

A small resource loop: **Gather +5** (`inventory.grant("wood",5)`) and **Build −10**
(`inventory.consume("wood",10)` → status shows "not enough wood" on 409). The panel
shows the current `wood` count (refreshed after each action / on login).

## 6. Build order

- **S3.1** inventory table + `InventoryService` (get/list/grant/consume; item + amount
  validation; non-negative consume → 409). → ch.60.
- **S3.2** `InventoryController` + routes behind both filters.
- **S3.3** integration test `baas_inventory`: grant→get, list, consume, insufficient
  (409), bad item (400), bad amount (400), per-user + cross-tenant isolation, JWT req.
- **S3.4** SDK `inventory()` + unit (fake) + `sdk_live` extension.
- **S3.5** colony wood loop (Gather/Build) native + web.
- **S3.6** acceptance: guidebook ch.60, overview/README, security grep, ASan/UBSan,
  self-review, merge `--no-ff`.

## 7. Security

Project+user scoping on every query (isolation test); user from JWT; parameterized
SQL; item whitelist; amount bounds; **server-enforced non-negative consume** (no
client-side over-spend). Same posture as the reviewed leaderboard/cloud-save.
