# Chapter 81 — The Asset Registry: Track B Meets Track C

> Code: `baas/asset_registry/asset_service.{h,cc}` + `asset_controller.{h,cc}`,
> `baas/db/db.cc` (the `assets` table), `sdk/cpp` (`Client::Assets`);
> tests `tests/test_baas_assets.cc`, `tests/test_sdk_live.cc`.

The Mini-Studio *makes* assets — `.hrt` textures, `.map` levels. The BaaS is where a game's
data *lives*. This chapter connects them: an **asset registry** endpoint the game (or the
operator) uploads assets to, lists, downloads, and deletes — over authenticated HTTP. It is
the first new BaaS module of the platform expansion, and it is almost entirely a *mirror* of
an existing one, which is the real lesson: in a codebase with a clear module shape, a new
feature is mostly deciding **what's different**, then copying the rest.

## 1. One decision that shapes everything: scope

The closest existing module is **cloud save** (ch. on cloud saves): a named key/value store.
Saves are scoped to a **player** — keyed `(project_id, user_id)` — because your save is
yours. But an *asset* is not yours; it is the *game's*. Every player of a game sees the same
wall texture and the same level. So the registry is scoped to the **project only**:

- **Table key:** `UNIQUE(project_id, name)` — no `user_id` column at all.
- **Auth:** the routes require *only* the api-key gate (`web::ApiKeyFilter`, which resolves
  `kProjectId`), **not** the per-user JWT gate (`web::AuthFilter`). A client with just an api
  key — no login — can read assets, exactly as a game does at startup before any player signs
  in.

That single decision — project vs user — is the *only* real difference from cloud save.
Everything else (optimistic concurrency, validation, the JSON envelope, the error shape) is
copied. The test proves the auth difference directly: every call uses only `X-Api-Key`, no
`Authorization` header, and they all succeed.

## 2. Mirror the module

The BaaS has a consistent two-layer shape (see the module-anatomy of any service): a **pure
service** (`web::asset`, DB logic, no Drogon) and a **thin controller** (`AssetController`,
HTTP edge). Writing the registry was filling in that shape:

```cpp
// service — the SQL, project-scoped
namespace web::asset {
  PutResult             put(long project_id, const std::string& name, const std::string& kind,
                            const std::string& data, long long if_match);
  std::optional<Record> get(long project_id, const std::string& name);
  std::vector<Meta>     list(long project_id, const std::string& kind_filter);
  bool                  remove(long project_id, const std::string& name);
}
```

```cpp
// controller — routes, api-key gate ONLY
ADD_METHOD_TO(AssetController::put,  "/v1/assets/{name}", drogon::Put, "web::ApiKeyFilter");
ADD_METHOD_TO(AssetController::list, "/v1/assets",        drogon::Get, "web::ApiKeyFilter");
```

The controller pulls `kProjectId` off the request (the filter put it there), validates, calls
the service, and maps the result to the shared `{"error":{...}}` / success JSON envelope. If
you have read the cloud-save controller, you have read this one — which is the point of a
consistent codebase.

## 3. Opaque text, and the base64 rule

`data` is stored as **TEXT**, opaque to the server — the same choice cloud save makes. A
`.map` level *is* text, so it goes verbatim. A `.hrt` texture is **binary**, and binary does
not belong in a UTF-8 TEXT column or a JSON string. The rule is therefore: **the caller
base64-encodes binary assets.** The registry never interprets the payload, so it does not care
— but the convention has to be written down, because "store bytes" and "store text" look the
same until a NUL byte corrupts a row. (A later slice could push base64 into the SDK so callers
never think about it; for now it is documented and the caller's job.)

`kind` is a small free tag (`"texture"`, `"level"`) — not interpreted either, except as a
list filter: `GET /v1/assets?kind=texture`. It is the one affordance that makes a flat
registry browsable.

## 4. `list` with an optional filter, without SQL injection

The one place the service does more than cloud save is the filtered list. The lazy-wrong way
is to concatenate the filter into the SQL. The right way is two prepared statements and a
branch:

```cpp
const auto rows = kind_filter.empty()
    ? db::client()->execSqlSync(sql_all,  project_id)
    : db::client()->execSqlSync(sql_kind, project_id, kind_filter);   // ? binding, never string-cat
```

Both queries bind their parameters; the filter value is never interpolated. Two statements is
more code than one f-string — and it is the code that does not have an injection hole.

## 5. The SDK side

A game consumes the registry through the SDK's new `Assets` handle, a mirror of `Saves`:

```cpp
c.assets().put("level_00.map", "level", map_text, [](gbaas::Result<gbaas::AssetMeta> r){ ... });
c.assets().get("wall_1.hrt", [](gbaas::Result<gbaas::Asset> r){ /* r->data */ });
```

Non-blocking like the rest of the SDK (results arrive on `Client::update()`), api-key
auto-attached, no login required. `test_sdk_live` drives a real `put→get→list→remove` over
libcurl against the live server — end-to-end proof that a game can pull the assets the
Mini-Studio produced.

## 6. Where this sits

```
   Texture Lab / Map Lab  ──produce──►  .hrt / .map
                                            │  c.assets().put(name, kind, data)
                                            ▼
                          BaaS  /v1/assets  ── assets table (project-scoped) ──►  serve
                                            ▲
                                            │  c.assets().get(name)  → game loads it
                                          a game at startup (api key, no login)
```

Track B made the content; Track C now stores and serves it. The join is an HTTP endpoint and
a naming rule — the same "convention over mechanism" thread that runs through the textured
sprites, the level format, and the wall skins.

## Pitfalls

- **Copying cloud save's user scope.** Assets are the game's, not a player's. Drop `user_id`
  and the `AuthFilter`, or every player would have a private, invisible copy.
- **Storing binary in the TEXT column raw.** Base64 it; document the rule.
- **String-concatenating the `?kind=` filter into SQL.** Bind it. Always.
- **Forgetting the payload cap.** 1 MiB here; an uncapped upload is a storage-DoS.

## Glossary

- **project-scoped** — keyed by `project_id` alone; shared by every player of the game.
- **api-key gate** — `ApiKeyFilter`, resolves the project from `X-Api-Key`; the only auth the
  registry needs.
- **kind** — an opaque tag for list filtering (`texture`/`level`).
- **opaque payload** — the server never interprets `data`; binary is base64 by convention.

## Exercises

1. **Publish scope.** Add an admin/secret gate so only the operator can `PUT`/`DELETE` while
   any api key can `GET`. Which filter, and where in the chain?
2. **Blob on disk.** Store large assets as files with the DB holding only a path. What does
   `get` change, and what new failure modes appear?
3. **SDK base64.** Add `put_binary`/`get_binary` SDK helpers that base64 on the way out and
   decode on the way in, so games never think about encoding. Which layer owns it?
4. **Provision + serve a whole game.** Combine this with a `provision game` endpoint (create a
   project + api key) so an operator can stand up a game and its assets in two calls. What is
   the minimum the provision step must return?
