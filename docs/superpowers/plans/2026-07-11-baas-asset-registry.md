# BaaS Asset Registry — Implementation Plan

> TDD-ish (integration test is the proof). One behaviour per commit. Mirrors `cloud_save`.

**Goal:** project-scoped asset registry (`/v1/assets`) storing Mini-Studio `.hrt`/`.map` assets + an SDK handle.
**Architecture:** service (`web::asset`, pure DB logic) + Drogon controller (api-key gate only) + `assets` table;
integration test via real HTTP; SDK `Assets` handle over `Client::request<T>`. Deferrals: spec §9.

---

### Task 1: schema — `assets` table
- Add the `CREATE TABLE assets` block (spec §3) to `kSchemaSql` in `baas/db/db.cc` (before the closing `)SQL"`).
- Commit.

### Task 2: service `web::asset` (`baas/asset_registry/asset_service.{h,cc}`)
- Mirror `save_service`: `valid_name`([A-Za-z0-9._-], 1..128), `valid_kind`([A-Za-z0-9_-], 0..32),
  `put/get/list/remove` — project-scoped (no user_id), `kind` column, `list(kind_filter)` with an optional
  `AND kind=?` when non-empty. Optimistic `if_match` like save. SQL uses `assets` table.

### Task 3: controller `web::AssetController` (`baas/asset_registry/asset_controller.{h,cc}`)
- Mirror `save_controller` but filter list = `"web::ApiKeyFilter"` ONLY (no AuthFilter), no `uid`.
- Routes: PUT/GET/DELETE `/v1/assets/{name}`, GET `/v1/assets`. Body `{kind,data}`; cap 1 MiB → 413.
- `?kind=` on list via `req->getParameter("kind")`. JSON out: name/kind/version/size(/data/updated_at).

### Task 4: CMake + integration test
- `baas/CMakeLists.txt`: add `asset_registry/asset_service.cc` + `asset_registry/asset_controller.cc` to
  `baas_core OBJECT`; add `test_baas_assets` target (link `baas_core CURL::libcurl engine_flags`, `add_test`).
- `tests/test_baas_assets.cc`: mirror `test_baas_cloudsave.cc` harness. Cases (spec §7): put→get, version
  bump, list + `?kind=` filter, delete→404, If-Match 409/200, name/kind 400, cap 413, cross-tenant isolation,
  works with api-key alone (no JWT). Note: assets use **only** `keyA` header (no Bearer).
- Build `cmake --build build`; `ctest -R baas_assets` → PASS. Commit.

### Task 5: SDK `Assets` handle
- `client.h`: add `struct AssetMeta{name,kind,version,size}`, `struct Asset{name,kind,version,data}`, a
  `class Assets{put,get,list,remove}` mirroring `Saves`, and `Assets assets(){return Assets(this);}`.
- `client.cc`: implement the 4 methods via `request<T>` (extract lambdas parse the JSON). `put` body
  `{"kind":"...","data":"..."}` (json::escape both).
- `tests/test_sdk_live.cc`: append an assets put→get→list→remove round-trip. Build; `ctest -R sdk_live` → PASS.
- Commit.

### Task 6: docs + merge
- Guidebook `docs/book/81-baas-asset-registry.md` (project vs user scope, the api-key gate, mirror-a-module,
  opaque-text+base64, the Track B→C bridge, deferrals). README roadmap row.
- Merge `--no-ff`; delete branch; checkpoint memory.

## Self-review
- Scope difference locked: assets = project-scoped, api-key-only (no AuthFilter/uid) — the one deviation from
  `cloud_save`; everything else mirrors it. Types (`asset::Meta/Record/PutResult`, SDK `AssetMeta/Asset`)
  consistent across service/controller/test/SDK. `assets` table UNIQUE(project_id,name).
