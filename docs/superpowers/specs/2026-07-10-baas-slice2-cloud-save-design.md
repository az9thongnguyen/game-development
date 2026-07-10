# Game BaaS — Slice #2: Cloud Save (design + plan)

- **Date:** 2026-07-10
- **Status:** Approved (autonomous) — builds on Slice #1's spine
- **Builds on:** `2026-07-10-game-baas-slice1-auth-leaderboard-design.md`

## 1. Goal

The second L1 service: per-user **cloud save** — a game stores/loads its state under
named slots, from any device, once the player is signed in. It reuses the entire
Slice #1 spine (Drogon gateway, `ApiKeyFilter`→project, `AuthFilter`→user, SQLite via
`DbClient`, the non-blocking C++ SDK) and adds one table, one service, one controller,
one SDK handle. The colony demo gains **Cloud Save / Load** that round-trips its live
state (agents + props) — native and web.

## 2. Scope

**In:** a `saves` table; `SaveService` (put/get/list/remove); `SaveController`
(`PUT/GET/DELETE /v1/saves/{slot}`, `GET /v1/saves`) behind api-key + JWT; optimistic
concurrency via `If-Match`; a payload size cap; SDK `client.saves()`; colony
serialize/restore + Save/Load buttons; integration + unit tests; guidebook ch.59.

**Out:** binary/base64 payloads (Slice #2 stores **UTF-8 text**, e.g. JSON — the
demo serializes to JSON); per-slot ACLs; server-side merge/conflict resolution beyond
`If-Match`; quotas beyond the per-payload cap. (Named so the design leaves room.)

## 3. Data model (append to the existing idempotent migration)

```sql
CREATE TABLE IF NOT EXISTS saves (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  slot TEXT NOT NULL,
  data TEXT NOT NULL,                    -- UTF-8 payload (JSON for the demo)
  version INTEGER NOT NULL DEFAULT 1,    -- bumped on each write (optimistic concurrency)
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, user_id, slot)
);
```

Adding it to `kSchemaSql` is safe: `run_migrations` runs `CREATE TABLE IF NOT EXISTS`
every boot, so existing DBs gain the table on next start — no migration runner needed
yet (that lands when a migration must *alter* existing data).

## 4. API (all api-key + JWT; user from the token)

| Method + path | Body → Response |
|---|---|
| `PUT /v1/saves/{slot}` | `{data}` (+ optional `If-Match: <version>`) → `{slot, version, size}`; 409 on version mismatch; 413 if too large |
| `GET /v1/saves/{slot}` | → `{slot, version, data, updated_at}`; 404 if absent |
| `GET /v1/saves` | → `{saves:[{slot, version, size, updated_at}]}` (metadata only) |
| `DELETE /v1/saves/{slot}` | → `{deleted:true}`; 404 if absent |

`slot` is validated (non-empty, ≤ 64 chars, `[A-Za-z0-9_-]`). Payload cap
`kMaxSaveBytes = 256*1024`. Everything scoped by `(project_id, user_id)` from the
gateway — same tenant + anti-spoof guarantees as the leaderboard.

## 5. SDK — `client.saves()`

```cpp
struct SaveMeta { std::string slot; long long version; long long size; };
struct Save     { std::string slot; long long version; std::string data; };
client.saves().put(slot, data, cb);      // Result<SaveMeta>
client.saves().get(slot, cb);            // Result<Save>
client.saves().list(cb);                 // Result<std::vector<SaveMeta>>
client.saves().remove(slot, cb);         // Result<bool>
```

Payloads are UTF-8 strings embedded in the JSON envelope (`{"data":"…"}`), escaped by
the SDK's `json::escape` and un-escaped by the parser — so no new transport plumbing
(response headers) is needed. `If-Match` deferred in the SDK to "last-write-wins" for
now; the server still enforces it when a client sends the header.

## 6. Colony integration

`Sim::clear()` (reset the registry, keep the map) is added so a load can rebuild from
scratch. The scene serializes the ECS `view<Visual,GridPos>` to a small JSON string
(x, y, color, is_agent) with `gbaas::json`, `saves().put("colony", …)`; Load does
`saves().get` → parse → `sim_.clear()` → re-`spawn_agent`/`spawn_prop`. Buttons:
`Cloud Save`, `Load`.

## 7. Build order (each = code + test + review→fix)

- **S2.1** schema (saves table) + `SaveService` (put/get/list/remove, size/slot
  validation, version bump, `If-Match`). → ch.59.
- **S2.2** `SaveController` + routes behind both filters.
- **S2.3** integration test `baas_cloudsave`: put→get round-trip, version bump, list,
  delete, tenant/user isolation, size cap (413), bad slot (400), If-Match (409).
- **S2.4** SDK `saves()` + unit test (fake transport) + extend `sdk_live`.
- **S2.5** colony serialize/restore + `Sim::clear()` + buttons; build native + web.
- **S2.6** acceptance: guidebook ch.59, overview/README, security checklist, ASan/UBSan,
  self-review, merge `--no-ff`.

## 8. Security (same posture as Slice #1)

Project+user scoping on every query (isolation test); user from JWT never the body;
parameterized SQL; slot whitelist (`[A-Za-z0-9_-]`, ≤64) prevents odd keys; payload
cap prevents storage-DoS; data is stored/returned as-is (opaque to the server) — the
game owns its meaning.
