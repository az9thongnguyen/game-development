# Chapter 59 — Cloud Save (Slice #2)

> **What this is.** The second backend service, and the proof that the walking
> skeleton pays off: **cloud save** — per-user, per-slot game state that syncs
> across devices — was added by touching *one* table, *one* service, *one*
> controller, and *one* SDK handle. Everything else (gateway, auth, tenancy, the
> non-blocking client) came for free from Slice #1. You'll see versioned writes
> with optimistic concurrency, the payload/slot guards, and how the colony demo
> serializes its live ECS state to the cloud and back. Code: `baas/cloud_save/*`,
> SDK `client.saves()`, `src/games/colony/colony_scene.cpp`.

---

## 1. The shape (same spine, one more service)

```
game ─▶ SDK client.saves() ─▶ Gateway (api-key + JWT) ─▶ SaveController ─▶ SaveService ─▶ saves table
```

A save is an opaque **UTF-8 payload** (the game decides its meaning — the colony
stores JSON) under a named **slot**, owned by `(project_id, user_id)`. The service
never interprets the bytes; it just stores, versions, and returns them.

## 2. The table (no migration runner needed — yet)

```sql
CREATE TABLE IF NOT EXISTS saves (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  slot TEXT NOT NULL, data TEXT NOT NULL,
  version INTEGER NOT NULL DEFAULT 1,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, user_id, slot));
```

We simply appended this to the existing `kSchemaSql`. Because `run_migrations` runs
`CREATE TABLE IF NOT EXISTS` on every boot, an existing database gains the table on
its next start — idempotent, zero-ceremony. A real *versioned* migration mechanism
(a table of applied versions, ordered files) is the right tool the day a migration
must **alter** existing data; adding a brand-new table doesn't need it. Build the
machine when you have the problem it solves.

## 3. Versioned writes + optimistic concurrency

`version` starts at 1 and bumps on every write. That gives us **optimistic
concurrency** for free: a client that read version *N* can send `If-Match: N` on its
next write, and the server rejects with **409** if the stored version has moved on
(someone else — another device — saved in between). Without `If-Match` it's
last-write-wins. The whole rule is a few lines:

```cpp
// baas/cloud_save/save_service.cc
if (if_match > 0) {                              // caller requires a specific version
    if (existing.empty() || existing[0]["version"].as<long>() != if_match)
        return {std::nullopt, Error{409, "version_conflict", "save was modified"}};
}
long long new_version = 1;
if (existing.empty())  INSERT … version 1;
else { new_version = cur + 1; UPDATE … SET version=new_version …; }
```

Every query is scoped by **both** `project_id` and `user_id` — the same isolation
discipline as the leaderboard, now two-dimensional: a player can only ever see and
touch *their own* saves within *their* project. The integration test proves it with
a second user and a second project.

## 4. The API and its guards

| Method + path | Body → Response |
|---|---|
| `PUT /v1/saves/{slot}` | `{data}` (+ optional `If-Match: N`) → `{slot, version, size}`; 409 / 413 |
| `GET /v1/saves/{slot}` | → `{slot, version, data, updated_at}`; 404 |
| `GET /v1/saves` | → `{saves:[{slot, version, size, updated_at}]}` (metadata only) |
| `DELETE /v1/saves/{slot}` | → `{deleted:true}`; 404 |

Two guards keep it safe and small: **slot validation** (`valid_slot`: 1–64 chars of
`[A-Za-z0-9_-]`, rejecting odd keys with **400**) and a **payload cap**
(`kMaxSaveBytes = 256 KiB`, rejecting with **413**) so no client can fill the store.
Both are cheap and both are tested.

## 5. The SDK handle

Identical ergonomics to `auth()`/`leaderboard()`:

```cpp
client.saves().put("colony", stateJson, [&](gbaas::Result<gbaas::SaveMeta> r){ … });
client.saves().get("colony",            [&](gbaas::Result<gbaas::Save> r){ … });
client.saves().list([&](gbaas::Result<std::vector<gbaas::SaveMeta>> r){ … });
client.saves().remove("colony",         [&](gbaas::Result<bool> r){ … });
```

The payload rides inside the JSON envelope (`{"data":"…"}`), escaped by the SDK's
`json::escape` and un-escaped by the parser — so no new transport plumbing (response
headers) was needed. Same non-blocking model: results arrive on `client.update()`.

## 6. Colony: saving a live world

The colony demo gained **Cloud Save** and **Load** buttons. Save walks the ECS
`view<Visual, GridPos>` and emits a compact JSON array of `{x, y, color, is_agent}`;
Load parses it, calls the new `Sim::clear()` (wipe entities, keep the map), and
re-spawns each entity:

```cpp
void ColonyScene::cloud_load() {
    client_.saves().get("colony", [this](gbaas::Result<gbaas::Save> r) {
        if (!r) { status_ = "no cloud save"; return; }
        const auto j = gbaas::json::parse(r->data);
        if (!j) { status_ = "bad save data"; return; }
        sim_.clear();
        for (…each entry…) it.a ? sim_.spawn_agent(x,y,c) : sim_.spawn_prop(x,y,c);
    });
}
```

Play, spawn some colonists, **Cloud Save**, **Reset**, then **Load** — the colony
comes back. Works native and in the browser (same SDK, same transport seam).

## 7. What this slice really demonstrated

Not the feature — the **shape**. Cloud save is proof that adding an L1 service is now
a bounded, repeatable move: a table (scoped by tenant + user), a service (with its
few guards), a controller behind the existing filters, an SDK handle, and a test
suite that boots the real app. Inventory, Remote Config, Analytics, and Live Events
are the same move again.

## 8. Checkpoints

- Why does appending a `CREATE TABLE IF NOT EXISTS` to the migration string safely
  upgrade an existing database?
- Walk through an `If-Match` conflict: two devices, one slot. Who gets the 409, and why?
- Cloud save queries are scoped by two columns, not one. Which two, and what would a
  missing `user_id` clause leak?
