# Game BaaS — Slice #1 (Auth + Leaderboard) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a real, demoable end-to-end vertical — colony game → C++ SDK → Drogon Gateway → Auth → Leaderboard → DB → back — running both native and WASM.

**Architecture:** A single Drogon process (modular monolith) under `baas/`: a Gateway filter chain (`ApiKeyFilter` → project, `AuthFilter` → user via JWT) in front of `Auth` and `Leaderboard` modules (each `Controller` = HTTP edge, `Service` = unit-testable logic), persisting through Drogon's `DbClient` (SQLite dev / Postgres prod, one parameterized-SQL codebase). A separate `gbaas_sdk` C++ library gives games a non-blocking client whose HTTP transport is a seam (`libcurl` native / `emscripten_fetch` web), mirroring the engine's platform seam. The colony game consumes the SDK.

**Tech Stack:** C++20, Drogon 1.9.13, libsodium (argon2id), jwt-cpp (HS256), libcurl, emscripten_fetch, SQLite 3 / Postgres 18, CMake, existing engine `ui::Context` for the in-game panel.

## Global Constraints

- **C++20**, built with CMake; keep `-Wall -Wextra -Wpedantic` clean (existing `engine_flags`).
- **Dependency isolation (hard):** Drogon / libsodium / jwt-cpp appear **only** in `baas/`. The engine core gains **no** new dependency. `gbaas_sdk` is a game-facing library whose only native dep is **libcurl** (web build: no dep, uses `emscripten_fetch`). Never link Drogon into a game or the engine.
- **SDL2 only inside `src/platform/`** — unchanged; the SDK does not touch SDL.
- **Web-portability:** the SDK is **non-blocking**; results are delivered when `Client::update()` is pumped from the game tick. No blocking network call above the transport seam. The web transport uses `emscripten_fetch` (async).
- **Security (non-negotiable):** parameterized SQL only (no string interpolation); passwords hashed with argon2id; JWT HS256 with a short expiry; JWT secret + DB credentials from env/config, **never committed** (`.gitignore` covers `baas/config.json`, `*.db`, `.env`); **every** query filtered by `project_id`; score `user_id` taken from the verified JWT, never the request body; no user enumeration on login.
- **SQL portability:** conservative SQL that runs on both SQLite and Postgres.
- **Git:** work on `feat/baas-slice1-auth-leaderboard`; commit per task; merge `--no-ff` into `main` at S1.8.
- **Docs:** each task delivers/updates its guidebook chapter (51–58); split small, one concept per file.
- **GateGuard:** state facts before the first Bash command in a stretch.

---

## File structure (locked here)

```
baas/
  CMakeLists.txt              # 'baas' executable target (Drogon, libsodium, jwt-cpp)
  main.cc                     # args/config → DbClient → register filters+controllers → app().run()
  config.example.json         # port, host, db_url, jwt_secret (placeholder), cors_origins
  common/
    errors.{h,cc}             # Response make_error(status, code, message) → JSON envelope
    context_keys.h            # attribute keys for project_id / user_id on the request
  gateway/
    api_key_filter.{h,cc}     # HttpFilter: X-Api-Key → project_id (attr) or 401
    auth_filter.{h,cc}        # HttpFilter: Bearer JWT → user_id (attr), verify pid, or 401
  auth/
    jwt.{h,cc}                # issue(user_id,project_id) / verify(token) → optional<Claims>
    password.{h,cc}           # hash(pw) / verify(pw, hash)  [libsodium argon2id]
    auth_service.{h,cc}       # register/login/guest logic over DbClient → User + token
    auth_controller.{h,cc}    # POST /v1/auth/{register,login,guest}
  leaderboard/
    lb_service.{h,cc}         # submit(best-upsert), top(N), rank_of(user) over DbClient
    lb_controller.{h,cc}      # GET top, POST scores, GET me
  db/
    db.{h,cc}                 # make_db_client(url), run_migrations(client), seed(client)
    migrations/001_init.sql
sdk/cpp/
  include/gbaas/gbaas.h       # umbrella
  include/gbaas/result.h      # Result<T>, Error
  include/gbaas/transport.h   # ITransport (async), HttpResponse
  include/gbaas/client.h      # Client, Config, Session/Rank/Board, auth()/leaderboard()
  src/client.cc              # request assembly, token store, pump(update), JSON parse
  src/transport_curl.cc      # native (libcurl multi, non-blocking)
  src/transport_emscripten.cc# web (emscripten_fetch)
  src/json.hpp               # tiny JSON (reuse engine serialize or vendored single-header)
  CMakeLists.txt             # 'gbaas_sdk' library target
tests/
  test_baas_jwt.cc           # unit: jwt + password
  test_baas_auth.cc          # integration: register/login/guest on temp SQLite
  test_baas_leaderboard.cc   # integration: submit/top/me + tenant isolation + spoof
  test_sdk_client.cc         # unit: Client over a FakeTransport
src/games/colony/colony_scene.{hpp,cpp}   # + online panel (guest login, submit, board)
docs/book/51..58-*.md
CMakeLists.txt (root)        # add_subdirectory(baas), (sdk), new tests, link colony
```

**JSON on the client:** vendor `nlohmann/json` single-header into `sdk/cpp/src/json.hpp` (header-only, no build-system dep). Decided here to unblock the SDK; the server uses Drogon's built-in `Json::Value`.

---

## Task S1.0: Dependencies + Drogon skeleton (`/healthz`)

**Files:**
- Create: `baas/CMakeLists.txt`, `baas/main.cc`, `baas/config.example.json`
- Modify: `CMakeLists.txt` (root — `add_subdirectory(baas)` guarded by `find_package(Drogon)`)
- Modify: `.gitignore` (`baas/config.json`, `*.db`, `.env`)

**Interfaces:**
- Produces: a runnable `baas` binary serving `GET /healthz` → `200 {"status":"ok"}`.

- [ ] **Step 1: Install deps.** `brew install drogon libsodium jwt-cpp` (Drogon pulls jsoncpp/uuid/zlib). Verify: `brew list --versions drogon libsodium jwt-cpp`.
- [ ] **Step 2: Minimal `main.cc`** — parse `--port/--host`, register a lambda handler for `/healthz`, `drogon::app().addListener(host,port).run()`. Keep it under ~40 lines; this only proves Drogon builds and boots.
- [ ] **Step 3: `baas/CMakeLists.txt`** — `find_package(Drogon CONFIG REQUIRED)`, `add_executable(baas main.cc)`, `target_link_libraries(baas PRIVATE Drogon::Drogon)`. Root CMake: `find_package(Drogon CONFIG QUIET)` then `if(Drogon_FOUND) add_subdirectory(baas) endif()` so the engine build never hard-depends on Drogon.
- [ ] **Step 4: Build + boot.** `cmake -B build -S . && cmake --build build --target baas`. Run `./build/baas/baas --port 8080 &`; `curl -s localhost:8080/healthz` → `{"status":"ok"}`; kill it.
- [ ] **Step 5: Commit.** `git add baas CMakeLists.txt .gitignore && git commit -m "baas(S1.0): Drogon skeleton + /healthz"`

**Deliverable / gate:** Drogon builds on this machine and answers a request. This de-risks the single biggest unknown before any logic. If the build fails, fix the CMake/brew integration here and update later tasks accordingly (auto-loop).

---

## Task S1.1: DB layer + schema + seed

**Files:**
- Create: `baas/db/db.h`, `baas/db/db.cc`, `baas/db/migrations/001_init.sql`
- Modify: `baas/main.cc` (init DbClient, run migrations, `--seed` path), `baas/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `web::db::make_db_client(const std::string& url) -> drogon::orm::DbClientPtr` — accepts `sqlite://PATH` and `postgres://...`.
  - `web::db::run_migrations(DbClientPtr)` — idempotent (creates tables if absent).
  - `web::db::seed(DbClientPtr)` — inserts one project (public_key `pk_demo_colony`) + a `colony_high` leaderboard (`sort='desc'`) if not present; prints the public key.

- [ ] **Step 1: `001_init.sql`** — the four tables from spec §6. Conservative types:

```sql
CREATE TABLE IF NOT EXISTS projects (
  id INTEGER PRIMARY KEY, name TEXT NOT NULL,
  public_key TEXT NOT NULL UNIQUE, secret_key_hash TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT (datetime('now')));
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY, project_id INTEGER NOT NULL REFERENCES projects(id),
  email TEXT, password_hash TEXT, display_name TEXT NOT NULL,
  is_guest INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL DEFAULT (datetime('now')));
CREATE UNIQUE INDEX IF NOT EXISTS ux_users_email ON users(project_id, email) WHERE email IS NOT NULL;
CREATE TABLE IF NOT EXISTS leaderboards (
  id INTEGER PRIMARY KEY, project_id INTEGER NOT NULL REFERENCES projects(id),
  key TEXT NOT NULL, name TEXT NOT NULL, sort TEXT NOT NULL DEFAULT 'desc',
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  UNIQUE(project_id, key));
CREATE TABLE IF NOT EXISTS scores (
  id INTEGER PRIMARY KEY, leaderboard_id INTEGER NOT NULL REFERENCES leaderboards(id),
  user_id INTEGER NOT NULL REFERENCES users(id), value BIGINT NOT NULL,
  updated_at TEXT NOT NULL DEFAULT (datetime('now')), UNIQUE(leaderboard_id, user_id));
```

> Note during impl: `datetime('now')` is SQLite; for Postgres provide the parallel DDL or use `CURRENT_TIMESTAMP` (portable). Keep a `_pg` variant if needed — verify both compile in S1.8.

- [ ] **Step 2: `db.cc`** — `make_db_client` maps the url scheme to `drogon::orm::DbClient::newSqlite3Client` / `newPgClient`. `run_migrations` executes the SQL (split on `;`), `seed` uses parameterized inserts guarded by existence checks.
- [ ] **Step 3: Wire into `main.cc`** — build the client from `--db` (default `sqlite://baas.db`), `run_migrations`, and if `--seed` was passed, `seed()` then exit.
- [ ] **Step 4: Verify.** `./build/baas/baas --db sqlite://./baas.db --seed` prints the public key; `sqlite3 baas.db ".tables"` shows the four tables; re-running `--seed` does not duplicate.
- [ ] **Step 5: Commit.** `git commit -am "baas(S1.1): DbClient, schema migration, seed"`

**Deliverable / gate:** a seeded DB with the four tables; migrations idempotent.

---

## Task S1.2: Gateway — errors + `ApiKeyFilter`

**Files:**
- Create: `baas/common/errors.{h,cc}`, `baas/common/context_keys.h`, `baas/gateway/api_key_filter.{h,cc}`
- Modify: `baas/main.cc` (register filter on `/v1/**`), `baas/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `web::make_error(int status, std::string code, std::string message) -> HttpResponsePtr` → body `{"error":{"code","message"}}`.
  - `ApiKeyFilter` (a `drogon::HttpFilter`) that on success sets request attribute `project_id` (key `web::kProjectId`) and continues; on missing/unknown key returns `401 {"error":{"code":"unauthorized",...}}`.

- [ ] **Step 1: Failing integration test** in `tests/test_baas_auth.cc` (bootstrap the harness here): start the app on an ephemeral port with a temp SQLite DB seeded; `curl`/Drogon `HttpClient` `GET /v1/leaderboards/colony_high/top` **without** `X-Api-Key` → expect `401`. Run: expect FAIL (no filter yet / route 404).
- [ ] **Step 2: Implement `errors` + `context_keys`** as above.
- [ ] **Step 3: Implement `ApiKeyFilter::doFilter`** — read `X-Api-Key`; `SELECT id FROM projects WHERE public_key=?`; if found set attribute + `fccb()` (continue), else `fcb(make_error(401,"unauthorized","invalid api key"))`.
- [ ] **Step 4: Register** the filter for the `/v1/` path prefix in `main.cc`; add a throwaway `GET /v1/ping` (behind the filter) returning the resolved `project_id` to make the test assert both the 401 and the 200-with-key paths.
- [ ] **Step 5: Run tests** → the 401 test passes; add a 200 test with the seeded key. Run: expect PASS.
- [ ] **Step 6: Commit.** `git commit -am "baas(S1.2): error envelope + ApiKeyFilter (multi-tenancy gate)"`

**Deliverable / gate:** every `/v1` request is project-scoped; unknown keys rejected.

---

## Task S1.3: Auth — password, JWT, service, controller

**Files:**
- Create: `baas/auth/password.{h,cc}`, `baas/auth/jwt.{h,cc}`, `baas/auth/auth_service.{h,cc}`, `baas/auth/auth_controller.{h,cc}`, `baas/gateway/auth_filter.{h,cc}`
- Create: `tests/test_baas_jwt.cc`
- Modify: `baas/main.cc`, `baas/CMakeLists.txt`, root CMake (`test_baas_jwt`)

**Interfaces:**
- Produces:
  - `web::pw::hash(std::string) -> std::string` / `web::pw::verify(pw, hash) -> bool` (argon2id).
  - `web::jwt::issue(long user_id, long project_id, std::string secret, int ttl_sec) -> std::string`
  - `web::jwt::verify(token, secret) -> std::optional<Claims{long sub; long pid;}>`
  - `AuthFilter` → sets attribute `user_id` (`web::kUserId`) from `Bearer` token, asserts `pid == project_id`, else 401.
  - Routes: `POST /v1/auth/register|login|guest` → `200 {"user":{...},"access_token":"..."}`.

- [ ] **Step 1: Unit tests** `test_baas_jwt.cc`:

```cpp
// password round-trip + rejection
auto h = web::pw::hash("s3cret"); assert(web::pw::verify("s3cret", h));
assert(!web::pw::verify("wrong", h)); assert(h != "s3cret");        // never plaintext
// jwt round-trip + tamper/expiry
auto t = web::jwt::issue(7, 3, "k", 3600); auto c = web::jwt::verify(t, "k");
assert(c && c->sub == 7 && c->pid == 3);
assert(!web::jwt::verify(t, "other"));                              // wrong secret
assert(!web::jwt::verify(web::jwt::issue(7,3,"k",-1), "k"));        // expired
```
Run: FAIL (undeclared).
- [ ] **Step 2: `password.cc`** — libsodium `crypto_pwhash_str` / `_str_verify` (require `sodium_init() >= 0` at startup in `main`).
- [ ] **Step 3: `jwt.cc`** — jwt-cpp HS256 create with claims `sub`,`pid`,`iat`,`exp`; verify with leeway 0, catch → `nullopt`.
- [ ] **Step 4: Run unit tests** → PASS.
- [ ] **Step 5: `auth_service.cc`** — `register`(project_id,email,pw,name): reject dup email, `pw::hash`, insert, return user; `login`: fetch by (project,email), `pw::verify`, **identical error** for missing-user vs bad-pw; `guest`(project_id,name?): insert `is_guest=1`, no email/pw. All parameterized, all scoped by `project_id` from the request attribute.
- [ ] **Step 6: `auth_controller.cc`** — parse+validate body (required fields, sizes), call service, on success `issue` a JWT with the configured secret, return the user (never the password_hash) + token. Map service errors via `make_error`.
- [ ] **Step 7: `AuthFilter`** — verify `Bearer` token, set `user_id` attr, check `pid` matches request project.
- [ ] **Step 8: Integration tests** (`test_baas_auth.cc`): register→200+token; register dup→409; login ok→200; login wrong pw→401 (same body as unknown email); guest→200 with `is_guest`. Run: PASS.
- [ ] **Step 9: Commit.** `git commit -am "baas(S1.3): argon2id + JWT + auth (register/login/guest) + AuthFilter"`

**Deliverable / gate:** working authentication with guest support; tokens verified by the gateway; no plaintext, no enumeration.

---

## Task S1.4: Leaderboard service + controller

**Files:**
- Create: `baas/leaderboard/lb_service.{h,cc}`, `baas/leaderboard/lb_controller.{h,cc}`, `tests/test_baas_leaderboard.cc`
- Modify: `baas/main.cc`, `baas/CMakeLists.txt`, root CMake

**Interfaces:**
- Produces:
  - `LeaderboardService::submit(project_id, key, user_id, value) -> {rank, value, updated}` — best-upsert per the board's `sort`.
  - `::top(project_id, key, limit≤100) -> vector<Entry{rank,user_id,display_name,value}>`
  - `::rank_of(project_id, key, user_id) -> optional<Entry>`
  - Routes: `GET /v1/leaderboards/{key}/top?limit=N` (api-key), `POST .../scores {value}` (api-key+JWT), `GET .../me` (api-key+JWT).

- [ ] **Step 1: Failing tests** `test_baas_leaderboard.cc` (integration on temp SQLite, two seeded projects A & B):
  - submit as user → `top` shows them at rank 1; re-submit lower value → unchanged (`updated=false`); higher → updated.
  - **spoof test:** `POST scores` uses the JWT's user, not any body user field — submit as user X, assert the row is X's.
  - **tenant isolation:** submit under project A; `top` under project B's key does **not** see it.
  - `POST scores` without JWT → 401; absurd `value` (e.g. > 1e15) → 400.
  Run: FAIL.
- [ ] **Step 2: `lb_service.cc`** — resolve board id by `(project_id,key)` (404 if absent). `submit`: `INSERT ... ON CONFLICT(leaderboard_id,user_id) DO UPDATE SET value=excluded.value, updated_at=... WHERE excluded.value {>|<} scores.value` per sort (SQLite & PG both support `ON CONFLICT`). Compute rank with `SELECT count(*)+1 FROM scores WHERE leaderboard_id=? AND value {>|<} ?`. `top`: ordered join to `users` for `display_name`, `LIMIT ?` (clamped). All parameterized; all filtered by the board that belongs to `project_id`.
- [ ] **Step 3: `lb_controller.cc`** — `top` reads `limit` (clamp ≤100, default 10); `scores` takes `user_id` from `web::kUserId` attribute (JWT), validates `value` range; `me` uses the JWT user. Errors via `make_error`.
- [ ] **Step 4: Run tests** → PASS (all four cases).
- [ ] **Step 5: Commit.** `git commit -am "baas(S1.4): leaderboard service (best-upsert, top, me) + isolation/spoof tests"`

**Deliverable / gate:** authenticated leaderboard with correct ranking, tenant isolation, and anti-spoof — the backend half of the walking skeleton is complete and integration-tested.

---

## Task S1.5: C++ SDK — client + native transport

**Files:**
- Create: `sdk/cpp/include/gbaas/{result.h,transport.h,client.h,gbaas.h}`, `sdk/cpp/src/{client.cc,transport_curl.cc,json.hpp}`, `sdk/cpp/CMakeLists.txt`, `tests/test_sdk_client.cc`
- Modify: root CMake (`add_subdirectory(sdk/cpp)`, `test_sdk_client`)

**Interfaces:**
- Produces (the uniform SDK API):

```cpp
namespace gbaas {
struct Error { std::string code, message; int status = 0; };
template<class T> struct Result {                    // ok(value) or err(Error)
  std::optional<T> value; std::optional<Error> error;
  explicit operator bool() const { return value.has_value(); }
};
struct Session { long user_id; std::string display_name; bool is_guest; };
struct Rank    { long value; int rank; bool updated; };
struct Entry   { int rank; long user_id; std::string name; long value; };
struct Board   { std::vector<Entry> entries; };

struct HttpResponse { int status; std::string body; };
struct ITransport {                                   // the seam (native/web)
  virtual ~ITransport() = default;
  virtual void send(const std::string& method, const std::string& url,
                    const std::vector<std::pair<std::string,std::string>>& headers,
                    const std::string& body,
                    std::function<void(HttpResponse)> done) = 0;
  virtual void poll() = 0;                             // pump in-flight transfers
};

struct Config { std::string base_url; std::string api_key; };
class Client {
public:
  Client(Config, std::unique_ptr<ITransport>);        // native default = curl
  void update();                                       // call each frame → fires callbacks
  struct Auth { void guest(std::function<void(Result<Session>)>);
                void login(std::string email, std::string pw, std::function<void(Result<Session>)>);
                void registerUser(std::string email, std::string pw, std::string name,
                                  std::function<void(Result<Session>)>); };
  struct Lb   { void submit(long value, std::function<void(Result<Rank>)>);
                void top(int limit, std::function<void(Result<Board>)>);
                void me(std::function<void(Result<Rank>)>); };
  Auth auth();
  Lb   leaderboard(std::string key);
private:
  // holds Config, token_, transport_; builds headers (X-Api-Key always, Bearer after login)
};
} // namespace gbaas
```

- [ ] **Step 1: Unit test** `test_sdk_client.cc` with a `FakeTransport` (records the last request, returns a canned `HttpResponse`, `send` stores the callback; `poll`/`update` invokes it):

```cpp
FakeTransport* fake; gbaas::Client c({"http://x","pk_test"}, make_fake(&fake));
gbaas::Result<gbaas::Session> got{};
c.auth().guest([&](auto r){ got = r; });
fake->reply({200, R"({"user":{"user_id":5,"display_name":"g","is_guest":true},"access_token":"tok"})"});
c.update();                                            // pump → callback fires
assert(got && got.value->user_id == 5 && got.value->is_guest);
assert(fake->last.headers_have("X-Api-Key","pk_test"));
// after guest, token is attached to subsequent calls:
c.leaderboard("colony_high").submit(100, [](auto){});
assert(fake->last.headers_have("Authorization","Bearer tok"));
```
Run: FAIL.
- [ ] **Step 2: `result.h`/`transport.h`/`client.h`** exactly as the interface block.
- [ ] **Step 3: `json.hpp`** — vendor nlohmann/json single header (or reuse engine JSON if adequate).
- [ ] **Step 4: `client.cc`** — build URL from `base_url`+path; always add `X-Api-Key`; add `Bearer token_` when present; on auth success store `token_`; parse JSON → `Result<T>`; non-2xx → `Result` error from the `{"error":{...}}` envelope. `update()` calls `transport_->poll()`.
- [ ] **Step 5: `transport_curl.cc`** — libcurl **multi** interface (non-blocking): `send` adds an easy handle + header list, stashes the callback; `poll` runs `curl_multi_perform` and drains completed handles → invokes callbacks. No blocking `curl_easy_perform` in the hot path.
- [ ] **Step 6: Build + run unit test** (uses FakeTransport, no network) → PASS.
- [ ] **Step 7: Integration** — extend the test (guarded/opt-in) to run the real `CurlTransport` against a live `baas` on an ephemeral port: guest→submit→top round-trip. Run: PASS.
- [ ] **Step 8: Commit.** `git commit -m "baas(S1.5): unified non-blocking C++ SDK + libcurl transport"`

**Deliverable / gate:** a transport-agnostic SDK proven against a fake transport and a live server (native).

---

## Task S1.6: SDK web transport (`emscripten_fetch`)

**Files:**
- Create: `sdk/cpp/src/transport_emscripten.cc`
- Modify: `sdk/cpp/CMakeLists.txt` (select transport by `EMSCRIPTEN`), `CMakeLists.txt` web branch

**Interfaces:**
- Produces: `HttpTransportEmscripten : ITransport` — `send` issues `emscripten_fetch` (async, `EMSCRIPTEN_FETCH_LOAD_TO_MEMORY`), success/error handlers marshal to `HttpResponse` and invoke the stored callback; `poll` is a no-op (the browser event loop drives fetch). `Client` on web picks this transport.

- [ ] **Step 1:** implement `transport_emscripten.cc`; the fetch attr `userData` carries the callback (heap-owned, freed in the handler).
- [ ] **Step 2:** CMake — when `EMSCRIPTEN`, compile the emscripten transport + `-sFETCH`, exclude curl; else compile curl.
- [ ] **Step 3:** Build `gbaas_sdk` for web: `emcmake cmake` path builds without curl.
- [ ] **Step 4: Commit.** `git commit -m "baas(S1.6): emscripten_fetch transport for the web SDK"`

**Deliverable / gate:** the SDK compiles for WASM and uses `emscripten_fetch`; verified end-to-end in S1.7 in a real browser.

---

## Task S1.7: Colony integration (native + web)

**Files:**
- Modify: `src/games/colony/colony_scene.{hpp,cpp}` (SDK member, online panel, submit action), root CMake (link `gbaas_sdk` into the colony demo target), `web/shell.html` if the base URL needs injecting
- Modify: `baas/main.cc` — add a static-file route (serve `build-web/`) OR document using the existing `server/` for hosting the WASM + CORS to the baas

**Interfaces:**
- Consumes: `gbaas::Client`, `auth().guest`, `leaderboard("colony_high").{submit,top,me}`.
- Produces: an in-game online panel and a score submitted from colony.

- [ ] **Step 1:** add `gbaas::Client client_;` to `ColonyScene`; construct with base URL + the seeded public key (`pk_demo_colony`); on first `update`, `client_.auth().guest(...)` and store the session.
- [ ] **Step 2:** define the colony score metric (e.g. cumulative agents that reached their goal); a "Submit score" UI button → `client_.leaderboard("colony_high").submit(score_, cb)`; a "Leaderboard" toggle → `top(10)` and draw entries with the existing `ui::Context` panel.
- [ ] **Step 3:** pump `client_.update()` every frame in `ColonyScene::update`.
- [ ] **Step 4: Native verify** — run `baas` (seeded) + `./build/demo --colony`; guest-login, submit, see yourself on the board; a second guest sees both. Confirm via the DB.
- [ ] **Step 5: Web verify** — build colony.wasm with `gbaas_sdk`; serve it; enable CORS for the page origin on `baas`; use chrome-devtools MCP to load the page, submit a score, and confirm the network calls + the board render. (Stray Chrome profile? `pkill -f "chrome-devtools-mcp/chrome-profile"`.)
- [ ] **Step 6: Commit.** `git commit -am "baas(S1.7): colony online — guest login, submit, leaderboard panel (native + web)"`

**Deliverable / gate:** the walking skeleton is real end-to-end on both targets.

---

## Task S1.8: Acceptance — docs, hardening, review loop, merge

**Files:**
- Create: `docs/book/51..58-*.md` (finalize any not yet written per task)
- Modify: `docs/book/00-overview.md` (reading order + roadmap), `README.md` (run instructions for `baas` + colony online), `requirements.md` (mark §11 progress)

- [ ] **Step 1: Guidebook** — ensure chapters 51–58 exist and are complete (each task should have drafted its chapter; polish here). Update overview + README.
- [ ] **Step 2: Security checklist pass** (spec §11) — grep for SQL string-concatenation; confirm secrets are config/env + gitignored; confirm every service query filters `project_id`; re-run the tenant-isolation + spoof tests.
- [ ] **Step 3: Sanitizers** — build the tests under ASan/UBSan (`-DENGINE_SANITIZE=ON`), run the full CTest suite (existing 14 + the 4 new). All green.
- [ ] **Step 4: Postgres portability check** — run the integration suite once against a local Postgres 18 DB (`--db postgres://...`); fix any SQL that diverged from SQLite. Document the result.
- [ ] **Step 5: cpp-reviewer + security-reviewer** subagents over `baas/` and `sdk/cpp/`; fix findings; re-test (auto-loop).
- [ ] **Step 6: Merge.** `git checkout main && git merge --no-ff feat/baas-slice1-auth-leaderboard`; verify clean tree, full CTest green; delete the feature branch.

**Deliverable / gate:** Slice #1 shipped, documented, reviewed, tested on SQLite + Postgres, native + web, merged to `main`.

---

## Self-review (plan vs. spec)

- **Spec coverage:** architecture → S1.0/S1.2; multi-tenancy/project → S1.1 seed + S1.2 ApiKeyFilter; data model → S1.1; API contract → S1.3 (auth) + S1.4 (leaderboard); auth design → S1.3; SDK design → S1.5/S1.6; colony integration → S1.7; security §11 → enforced per task + audited S1.8; testing §12 → unit (S1.3/S1.5) + integration (S1.2/S1.3/S1.4) + e2e (S1.7); build/deploy §13 → S1.0 CMake + S1.7 hosting + S1.8 Postgres; docs §14 → chapters folded into each task + S1.8. All 8 sub-milestones (§15) mapped 1:1. No gaps.
- **Placeholder scan:** no TBD/TODO; the two spec open-questions are resolved here (nlohmann/json vendored; SQLite datetime portability flagged with the PG variant in S1.1/S1.8).
- **Type consistency:** `Result<T>`, `Session`, `Rank`, `Board`, `Entry`, `ITransport::send/poll`, `Client::update`, `auth()/leaderboard()`, and the service signatures (`submit/top/rank_of`, `jwt::issue/verify`, `pw::hash/verify`) are used identically across S1.3–S1.7. Attribute keys `web::kProjectId`/`web::kUserId` shared by S1.2/S1.3/S1.4.
