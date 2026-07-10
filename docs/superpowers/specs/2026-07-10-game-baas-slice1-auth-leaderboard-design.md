# Game BaaS — Slice #1: Auth + Leaderboard (walking skeleton)

- **Date:** 2026-07-10
- **Status:** Approved (design) — ready for implementation planning
- **Author:** Thong (+ Claude, mentor mode)
- **Supersedes / builds on:** `2026-06-21-native-webserver-design.md` (the 719-line
  blocking dev server). This slice is the first real step of turning that seed into a
  Backend-as-a-Service platform tightly integrated with the hand-written engine.

---

## 1. Context & goals

The repository is a hand-written C++20 game engine plus a collection of games, built as a
deep-learning exercise (mentor mode: complete code **and** a textbook guidebook). We are now
extending it into a **Game Backend-as-a-Service (BaaS)** so that games built on the engine get
online services (auth, leaderboard, cloud save, matchmaking, …) without hand-rolling
infrastructure per game.

**Confirmed end-state (the fork that shapes everything):** *learn deeply + hand-write the
service/architecture/SDK logic in C/C++, but stand on a proven C++ web framework for the
transport layer, and ship something that actually runs and demos end-to-end.* Hand-rolling
epoll/HTTP/TLS/WebSocket from scratch is explicitly **not** the goal — that effort goes into
architecture, service logic, and SDK design instead.

The full platform is ~15 independent subsystems (PlayFab/Nakama-scale). It **must** be built one
vertical slice at a time. This spec covers **Slice #1** only.

**Slice #1 goal:** a thin-but-100%-real vertical — `colony game → C++ SDK → Gateway (Drogon) →
Auth → Leaderboard → DB → back` — that proves the entire spine every later service will hang off.
Demoable as: *"log in from the colony game, submit your score, see the global leaderboard —
natively and in the browser (WASM)."*

## 2. Scope

**In scope (Slice #1):**
- Drogon-based backend process (modular monolith) with a Gateway filter chain.
- Multi-tenant **Project + API-key** model (seeded, no dashboard UI yet).
- **Auth:** email/password register + login, **guest/anonymous** login, JWT access tokens.
- **Leaderboard:** authenticated score submit (keep-highest), public top-N read, "my rank".
- **Persistence:** SQLite for dev/test, Postgres for prod — same parameterized SQL via Drogon's
  `DbClient`. Schema migrations + a seed.
- **Unified C++ SDK:** non-blocking client with a transport seam (native `libcurl` + web
  `emscripten_fetch`), covering auth + leaderboard.
- **Colony integration:** guest login on start, submit-score action, leaderboard panel drawn with
  the engine's existing immediate-mode UI — running both native and WASM.
- Tests (unit + integration) and a full guidebook arc (chapters 51–58).

**Out of scope (later slices):** dashboard web UI (L3); Cloud Save, Inventory, Remote Config,
Analytics, Live Events (L1, each its own slice); Lobby, Matchmaking + WebSocket (L2); dedicated
hosting, voice, anti-cheat, replay, cross-platform login, telemetry, AI services (L4); Unity (C#)
and Unreal SDKs (separate slices once the wire protocol is stable); TLS termination, refresh
tokens, email verification, password reset, admin API. These are named so the architecture leaves
room for them, but they are **not built here.**

## 3. Architecture

### 3.1 Component topology

Slice #1 runs as a **single Drogon process (modular monolith)** — not microservices.

```
NATIVE game (colony)            WEB game (colony.wasm)
   │ SDK C++ (one codebase)         │ SDK C++ (one codebase)
   │ transport: libcurl             │ transport: emscripten_fetch
   └───────────────┬────────────────┘
                   │  REST/JSON over HTTP(S)      (WebSocket added at L2)
                   ▼
          ┌─────────────────────┐
          │  Gateway (Drogon)   │  ApiKeyFilter → project   AuthFilter → user (JWT)
          │  + rate-limit, CORS │
          └──────────┬──────────┘
         ┌───────────┼────────────┐
         ▼           ▼            ▼
   AuthController  LeaderboardCtrl  (future services plug in here)
         │           │
         ▼           ▼   service layer (pure-ish logic, unit-testable)
   AuthService    LeaderboardService
         └───────────┼────────────┘
                     ▼  drogon::orm DbClient (parameterized SQL)
           SQLite (dev/test)  │  Postgres (prod)
```

**Why modular monolith, not microservices (decision + alternative):** the vision says "independent
services," which is correct *at scale*. But splitting into separate processes now buys network
hops, service discovery, and deploy complexity with zero load to justify it. Instead each service
is a **module with a clean interface** (`Controller` = HTTP edge, `Service` = logic, no
cross-service calls except through interfaces) inside one binary. The seams are placed so a service
can be lifted into its own process later (expected first at L2 realtime). *Chosen: monolith with
seams. Rejected: microservices-first (premature), single-file app (no seams → can't split later).*

### 3.2 Request lifecycle (the Gateway = Drogon filter chain)

Every request flows: `ApiKeyFilter` → (`AuthFilter` if the route needs a user) → Controller →
Service → DbClient → Response.

- **`ApiKeyFilter`** (all `/v1/*` routes): reads `X-Api-Key`, resolves it to a `project_id`,
  attaches it to the request context. Missing/invalid → `401`. This is the multi-tenancy gate.
- **`AuthFilter`** (routes needing a logged-in user): reads `Authorization: Bearer <jwt>`,
  verifies signature + expiry, attaches `user_id`. Missing/invalid/expired → `401`.
- Cross-cutting concerns (structured logging, CORS for the web build, basic per-IP rate-limit on
  `/auth/*`) live here too.

### 3.3 Transport & protocol

- **REST/JSON over HTTP** for Slice #1. Auth + leaderboard are request/response; REST is enough and
  trivially debuggable with `curl`. **WebSocket** is deferred to L2 (Lobby/Matchmaking need
  persistent connections). **gRPC** is YAGNI — add only if REST/JSON is measured to be the
  bottleneck. *Rejected building all three up front.*
- **HTTP for local dev; TLS terminated at a reverse proxy (Caddy/nginx) for prod.** Drogon can do
  TLS directly, but cert management is out of scope for Slice #1. Documented as the deploy step.

### 3.4 SDK transport seam (mirrors the engine's platform seam)

The colony game builds both native and WASM, so the SDK must run in both. This is solved exactly
like `platform.hpp` (`backend_sdl.cpp` vs `backend_web.cpp`):

```
gbaas::ITransport  (interface: async GET/POST → callback with status+body)
   ├── HttpTransportCurl        (native; libcurl)
   └── HttpTransportEmscripten  (web; emscripten_fetch, non-blocking)
```

The rest of the SDK (auth, leaderboard, JSON, token handling) is transport-agnostic and unit-tested
against a fake transport.

## 4. Technology choices

| Concern | Choice | Rationale / alternative rejected |
|---|---|---|
| Web framework | **Drogon 1.9.13** (brew) | Named in `requirements.md §11`; C++17 async, HTTP+WebSocket, built-in `DbClient` (PG/SQLite). Alt: Crow (no ORM), upgrade hand-written server (must re-write concurrency/WS/DB → too slow to "real"). |
| DB (dev/test) | **SQLite 3.53** (installed) | Zero-infra, instant demo, in-file test DBs. |
| DB (prod) | **Postgres 18** (installed) | Real concurrency/durability. Same SQL via `DbClient`. |
| Password hashing | **libsodium argon2id** (brew) | Never hand-roll crypto. Memory-hard, modern. Alt rejected: bcrypt (fine, but libsodium also gives us primitives for later). |
| JWT | **jwt-cpp** (brew, header-only) | HS256 signing/verify. Don't hand-roll token crypto. |
| HTTP client (native SDK) | **libcurl** | Ubiquitous, robust. |
| HTTP client (web SDK) | **emscripten_fetch** | The only non-blocking HTTP path in WASM. |
| JSON | Drogon's built-in JSON (server) + a tiny reuse of engine JSON on client, or nlohmann/json for the SDK | Decide in planning; keep SDK dep-light. |

## 5. Multi-tenancy & Project model

The platform differentiator ("create a project on a dashboard, toggle services, use immediately via
the SDK") starts here — the **data model** for it lands in Slice #1; the **dashboard UI** is L3.

- **Project = tenant.** Each has a `public_key` (embedded in the game, sent as `X-Api-Key`) and a
  `secret_key` (server-to-server / future admin; stored **hashed**).
- **Every table is scoped by `project_id`.** Every query filters on it — a bug here leaks data
  across tenants, so it gets dedicated isolation tests.
- **Slice #1 seeding:** one project + one leaderboard (`colony_high`) created by a migration/seed
  script or a `baas seed` CLI subcommand. No UI.

## 6. Data model

Portable SQL (works on SQLite and Postgres; types chosen for both). Migrations in
`baas/db/migrations/NNN_*.sql`.

- **`projects`** `(id PK, name, public_key UNIQUE, secret_key_hash, created_at)`
- **`users`** `(id PK, project_id FK, email, password_hash NULL, display_name, is_guest BOOL,
  created_at)` — `UNIQUE(project_id, email)` when email is present; guests have null email/password.
- **`leaderboards`** `(id PK, project_id FK, key, name, sort ENUM('desc','asc'), created_at)` —
  `UNIQUE(project_id, key)`.
- **`scores`** `(id PK, leaderboard_id FK, user_id FK, value BIGINT, updated_at)` —
  `UNIQUE(leaderboard_id, user_id)`; submit is an **upsert keeping the better value** per the
  board's sort.

## 7. API contract

All under `/v1`, all require `X-Api-Key` (→ project). Error model: JSON `{"error":{"code":"...",
"message":"..."}}` with appropriate HTTP status.

| Method + path | Auth | Body → Response |
|---|---|---|
| `POST /v1/auth/register` | api-key | `{email,password,display_name}` → `{user, access_token}` |
| `POST /v1/auth/login` | api-key | `{email,password}` → `{user, access_token}` |
| `POST /v1/auth/guest` | api-key | `{display_name?}` → `{user(is_guest), access_token}` |
| `GET  /v1/leaderboards/{key}/top?limit=N` | api-key | → `{entries:[{rank,user_id,display_name,value}]}` |
| `POST /v1/leaderboards/{key}/scores` | api-key **+ JWT** | `{value}` → `{rank, value, updated:bool}` |
| `GET  /v1/leaderboards/{key}/me` | api-key **+ JWT** | → `{rank, value}` or `404` if none |

Score writes take the `user_id` **from the verified JWT**, never from the body — a client cannot
submit for another user. `value` is range-checked; absurd values are rejected (full anti-cheat is
L4). `limit` is clamped (e.g. ≤ 100).

## 8. Authentication design

- **Register:** hash password with argon2id (libsodium `crypto_pwhash_str`), insert user, issue JWT.
- **Login:** `crypto_pwhash_str_verify`; on success issue JWT. Constant-time; identical error for
  unknown-email vs wrong-password (no user enumeration).
- **Guest:** create a `is_guest` user (no email/password), issue JWT. Games let players play first
  and register later — table-stakes. (Guest→registered upgrade is a later slice.)
- **JWT:** HS256 signed with a server secret (from config/env, **not** committed). Claims:
  `sub=user_id`, `pid=project_id`, `iat`, `exp` (short, e.g. 1h). No refresh token in Slice #1
  (deferred). `AuthFilter` verifies signature + expiry + that `pid` matches the request's project.

## 9. SDK design (unified C++ client)

- **Non-blocking, pumped in the game tick** — mirrors the engine's no-blocking-loop rule. Requests
  return immediately; results arrive via callback when `client.update()` is called (each frame).
  Never blocks the game loop; works identically native and web.
- **API shape (uniform across all services):** `client.<service>().<action>(args, callback)`,
  common `Result<T>` (`ok` + value, or `error{code,message}`), automatic `X-Api-Key` on every
  request and `Bearer` token after login.
  ```cpp
  gbaas::Client c({.base_url="http://127.0.0.1:8080", .api_key="<public>"});
  c.auth().guest([&](gbaas::Result<gbaas::Session> r){ if (r) score_ui.enable(); });
  c.leaderboard("colony_high").submit(4200, [&](gbaas::Result<gbaas::Rank> r){ ... });
  c.leaderboard("colony_high").top(10, [&](gbaas::Result<gbaas::Board> r){ ... });
  // in the game tick:
  c.update();   // pumps completed transfers → fires callbacks
  ```
- **Transport seam** (§3.4): `ITransport` + curl/emscripten impls; SDK logic tested against a fake
  transport with canned responses.
- **C++ is the base SDK.** Unity (C#) and Unreal wrap the same REST/JSON wire contract later — not
  in this slice.

## 10. Colony integration (the demo)

- On scene start: `client.auth().guest(...)` → enable the online panel.
- A "Submit score" action posts the current colony metric (e.g. agents delivered) to `colony_high`.
- A leaderboard panel drawn with the engine's **existing immediate-mode UI** shows top-N + my rank,
  refreshed on open. All calls pumped via `client.update()` in the scene's `update`.
- Builds and demos both native (`--colony`) and web (colony.wasm served by the backend's static
  route or the existing dev server).

## 11. Security requirements (not negotiable)

- Passwords: argon2id via libsodium; never stored or logged in plaintext.
- JWT secret + DB credentials from config/env, never committed; `.gitignore` covers them.
- SQL: parameterized queries only (`DbClient` binds) — no string interpolation.
- Multi-tenant isolation: every query filters `project_id`; dedicated cross-tenant leak tests.
- Score integrity: writes require a valid JWT; `user_id` from token, not body; server timestamps;
  value bounds-checked.
- No user enumeration on login; basic per-IP rate-limit on `/auth/*`.
- CORS locked to configured origins for the web build.
- Input validation at the trust boundary (body size limits, required fields, types).

## 12. Testing strategy

Keeps the project's "pure logic is unit-testable without I/O" ethos:
- **Unit:** JWT issue/verify, password hash/verify, ranking/upsert logic, request validation, SDK
  logic against a fake transport — all no-network.
- **Integration:** spin up the Drogon app on an ephemeral port against a temp **SQLite** DB; drive
  the real endpoints (register→login→submit→top→me), assert status + JSON. Includes the
  cross-tenant isolation test and the "can't spoof another user's score" test.
- **SDK end-to-end:** native SDK against the running test server.
- Registered in CTest alongside the existing 14 suites; run under ASan/UBSan.

## 13. Build, dev & deploy

- New CMake targets: `baas` (the server) and `gbaas_sdk` (the SDK library, linked by the colony
  game). Drogon/libsodium/jwt-cpp found via CMake `find_package`/pkg-config with brew prefixes,
  same pattern as the existing SDL discovery.
- **Dev:** `baas --db sqlite://baas.db --seed` then run; zero external infra.
- **Prod:** `baas --db postgres://…` behind Caddy/nginx for TLS. Optional `docker-compose`
  (Postgres + baas) documented but not required to demo.
- The engine core stays clean: the BaaS is a **separate process/target**; no Drogon/libsodium
  dependency ever enters the engine or the game except through the small `gbaas_sdk` client.

## 14. Documentation plan (guidebook, continuing from ch.50)

Split small, one concept per chapter (per user preference):
- **51** — BaaS overview & architecture (vision, layering, modular-monolith, why Drogon).
- **52** — Drogon foundations & the Gateway (filter chain, api-key/project, request lifecycle).
- **53** — Authentication (argon2id hashing, JWT, guest accounts, threat model).
- **54** — Persistence & the data model (DbClient, SQLite↔Postgres, migrations, tenant isolation).
- **55** — The Leaderboard service (schema, ranking upsert, authenticated writes).
- **56** — The unified C++ SDK (non-blocking client, transport seam native/web).
- **57** — Colony online: integrating the SDK (guest login, submit, leaderboard panel; native+web).
- **58** — Slice #1 acceptance (end-to-end walkthrough, security checklist, tests).

Plus updates to `docs/book/00-overview.md` (reading order + roadmap) and `README.md`.

## 15. Build order within the slice (sub-milestones)

Each sub-milestone = code + its guidebook chapter + tests + a review→test→fix loop before moving on.

- **S1.0** Dependencies + skeleton: brew-install Drogon/libsodium/jwt-cpp; `baas` CMake target;
  Drogon app boots; `GET /healthz`. → ch.52 (part).
- **S1.1** DB layer + migrations + seed: `DbClient`; 4 tables; seed one project + `colony_high`. → ch.54.
- **S1.2** Gateway: `ApiKeyFilter`, error model, CORS. → ch.52.
- **S1.3** Auth: argon2id, register/login/guest, JWT, `AuthFilter`. → ch.53.
- **S1.4** Leaderboard: submit (authenticated upsert), top, me; bounds. → ch.55.
- **S1.5** C++ SDK: client + curl transport + auth/leaderboard API + non-blocking pump; unit +
  integration tests. → ch.56.
- **S1.6** Web transport: `emscripten_fetch` impl; build colony.wasm against the SDK. → ch.56 (part).
- **S1.7** Colony integration: guest login, submit, leaderboard UI; native + web. → ch.57.
- **S1.8** Acceptance: end-to-end, security checklist, docs polish, review loop, merge `--no-ff`. → ch.58.

## 16. Out of scope / future slices

Cloud Save, Inventory, Remote Config, Analytics, Live Events (L1); Lobby, Matchmaking, WebSocket
(L2); dashboard UI (L3); dedicated hosting, voice, anti-cheat, replay, cross-platform login,
telemetry, AI services (L4); Unity/Unreal SDKs; TLS-in-app, refresh tokens, email verify, password
reset, guest→account upgrade, admin API.

## 17. Risks & open questions

- **Drogon learning curve / build integration on macOS** — mitigate in S1.0 with the smallest
  possible boot before any logic.
- **SQLite↔Postgres SQL portability** — keep SQL conservative; run the integration suite against
  both in CI eventually (SQLite now, Postgres before calling prod-ready).
- **WASM HTTP quirks** (CORS, async lifetime with `emscripten_fetch`) — isolated behind the
  transport seam and validated in S1.6 in a real browser.
- **JSON library on the client** — pick during planning to keep the SDK dependency-light.
- Everything else is deliberately deferred (see §16).
