# BaaS Asset Registry — Design Spec

**Date:** 2026-07-11 · **Track:** C (lean BaaS) — sub-project 1 · **Status:** approved (auto), implementing.

## 1. Goal

A **project-scoped asset registry** in the BaaS: a game uploads named assets (the Mini-Studio's
`.hrt` textures and `.map` levels), lists them, downloads them, deletes them — over authenticated
HTTP. This is the bridge from Track B (which *produces* assets) into Track C (which *stores and
serves* them), and the first new BaaS module of the expansion.

## 2. Why project-scoped (not per-user)

Cloud saves belong to a *player* (project_id + user_id). An asset belongs to the *game* — every
player of a game sees the same textures and levels. So assets are keyed by **(project_id, name)**
and the routes require **only** the api-key gate (`web::ApiKeyFilter`, which resolves `kProjectId`),
**not** the per-user JWT (`web::AuthFilter`). This is the meaningful design difference from
`cloud_save`, and it matches how a single-operator provisions content for their game.

> Deferred: an admin/publish scope so only the operator (not any api-key holder) can write. For a
> learning-first single-operator platform the api-key gate is the right lean choice; noted as a risk.

## 3. Data model

New table (added to the one embedded migration in `baas/db/db.cc`):

```sql
CREATE TABLE IF NOT EXISTS assets (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  name TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT '',
  data TEXT NOT NULL,
  version INTEGER NOT NULL DEFAULT 1,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(project_id, name)
);
```

`data` is opaque UTF-8 text, exactly like `cloud_save` — text assets (`.map`) go verbatim; **binary
assets (`.hrt`) are base64-encoded by the caller** (documented; the registry never interprets the
payload). `kind` is a free short tag (`"texture"`, `"level"`, …) used only for list filtering.

## 4. Service — `web::asset` (mirror of `web::save`, `baas/asset_registry/asset_service.{h,cc}`)

```cpp
namespace web::asset {
struct Meta   { std::string name, kind; long long version=0, size=0; std::string updated_at; };
struct Record { std::string name, kind; long long version=0; std::string data, updated_at; };
struct Error  { int status; std::string code, message; };
struct PutResult { std::optional<Meta> meta; std::optional<Error> error; };

bool valid_name(const std::string&);   // 1..128 chars, [A-Za-z0-9._-]  (filenames)
bool valid_kind(const std::string&);   // 0..32 chars, [A-Za-z0-9_-]    (empty allowed)

PutResult             put(long project_id, const std::string& name, const std::string& kind,
                          const std::string& data, long long if_match);   // upsert, version bump
std::optional<Record> get(long project_id, const std::string& name);
std::vector<Meta>     list(long project_id, const std::string& kind_filter);  // "" = all
bool                  remove(long project_id, const std::string& name);
}
```

Optimistic concurrency via `If-Match` (version), same as `cloud_save`.

## 5. Controller — `web::AssetController` (`baas/asset_registry/asset_controller.{h,cc}`)

Routes, **api-key only** (`"web::ApiKeyFilter"`):

- `PUT    /v1/assets/{name}` — body `{"kind":"texture","data":"..."}` → `{name,kind,version,size}`
- `GET    /v1/assets/{name}` — → `{name,kind,version,data,updated_at}` (404 if absent)
- `GET    /v1/assets`        — → `{"assets":[Meta,…]}`; optional `?kind=texture` filter
- `DELETE /v1/assets/{name}` — → `{"deleted":true}` (404 if absent)

`kProjectId` from the request attributes; payload cap **1 MiB** (`413` over). Errors via `make_error`.

## 6. SDK — `Client::Assets` handle (`sdk/cpp`, mirror of `Saves`)

```cpp
struct AssetMeta { std::string name, kind; long long version=0, size=0; };
struct Asset     { std::string name, kind; long long version=0; std::string data; };
class Assets { void put(name, kind, data, cb); void get(name, cb); void list(cb); void remove(name, cb); };
Assets assets() { return Assets(this); }
```

Built on the existing `Client::request<T>`; api-key auto-attached (no login needed for assets).

## 7. Tests

- **`tests/test_baas_assets.cc`** (integration, real HTTP via `baastest::http`, mirror
  `test_baas_cloudsave.cc`): put→get round-trip; update bumps version; list + `?kind=` filter; delete
  → 404; `If-Match` 409/200; name/kind validation (400); payload cap (413); **cross-tenant isolation**
  (project B sees none of A's); **no JWT required** (works with api-key alone).
- **SDK coverage:** extend `tests/test_sdk_live.cc` with a put→get→list→remove assets round-trip
  through the real SDK.

## 8. Files

- Modify: `baas/db/db.cc` (assets table), `baas/CMakeLists.txt` (2 sources + `test_baas_assets`),
  `sdk/cpp/include/gbaas/client.h` + `src/client.cc` (Assets handle), `tests/test_sdk_live.cc`
- Create: `baas/asset_registry/asset_service.{h,cc}`, `asset_controller.{h,cc}`, `tests/test_baas_assets.cc`
- Docs: guidebook `docs/book/81-baas-asset-registry.md`, README

## 9. Non-goals (deferred)

Publish/admin scope, blob-on-disk storage (vs DB text), server-side base64/content-type, CDN/edge
serving, asset version history/rollback, a dashboard asset browser, `provision game` + `sandbox test-runner`
(the other two Track-C asks — separate slices).
