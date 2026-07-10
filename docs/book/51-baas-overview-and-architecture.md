# Chapter 51 — Game BaaS: Overview & Architecture

> **What this is.** The first slice of turning the hand-written engine into a
> **Backend-as-a-Service (BaaS)** for games: online accounts and a global
> leaderboard that a game reaches through an SDK, exactly like PlayFab / Nakama /
> Unity Gaming Services do — but small, hand-reasoned, and yours. This chapter is
> the map: the whole platform's shape, why we build it one *vertical slice* at a
> time, why we stand on a framework here (when the engine hand-wrote everything),
> and the one architectural idea — the **transport seam** — that lets the same SDK
> run native and in the browser. Design: `docs/superpowers/specs/2026-07-10-*`.
> Code: `baas/` (a separate process) and `sdk/cpp/`.

---

## 1. The goal, stated honestly

The engine's rule was *"SDL is the only dependency, hand-write the rest."* A backend
is a different animal. A full game backend — auth, cloud save, leaderboards,
inventory, matchmaking, lobbies, analytics, live events, dashboards, plus SDKs for
three engines — is **fifteen-odd independent subsystems** and years of work for a
team. Hand-writing an HTTP/1.1 parser and a socket loop (which we *did* do in
Chapter 33, for fun and learning) does not scale to WebSockets, TLS, a connection
pool, and a SQL layer.

So the goal for this part of the project is deliberately different: **learn the
architecture and the service logic deeply, hand-write those, but stand on a proven
C++ web framework for the transport.** The learning goes into *how the pieces fit*
— gateways, filters, tokens, tenancy, ranking, an SDK that doesn't block a game
loop — not into re-deriving the HTTP state machine a second time.

## 2. One slice at a time

You cannot design fifteen subsystems at once, and you shouldn't. We grouped them
into layers by dependency and committed to building **one thin, complete vertical**
first — a *walking skeleton*:

```
colony game ─▶ C++ SDK ─▶ Gateway (Drogon) ─▶ Auth ─▶ Leaderboard ─▶ DB ─▶ back
```

It is intentionally narrow (only auth + leaderboard) but **100% real**: a genuine
database, hashed passwords, signed tokens, multi-tenant isolation, and a demo you
can run — *"log in from the colony game, submit your score, see the global board,
natively and in the browser."* Every later service (cloud save, inventory,
matchmaking, …) plugs into this same spine. Build the spine once; hang features off
it forever.

The layer plan (each its own spec → plan → build cycle later):

- **L0 — spine:** framework, Gateway, Auth, persistence, the project/tenant model, SDK core. *(this slice)*
- **L1 — stateless data services:** Cloud Save, Leaderboard, Inventory, Remote Config, Analytics, Live Events. *(Leaderboard is our L1 sample now)*
- **L2 — realtime:** Lobby, Matchmaking (need WebSockets + stateful sessions).
- **L3 — dashboard:** the web UI to create projects and toggle services.
- **L4 — advanced:** dedicated hosting, voice, anti-cheat, replay, telemetry, AI.

## 3. The layering (and the iron boundary)

```
NATIVE game (colony)            WEB game (colony.wasm)
   │ SDK C++ (one codebase)         │ SDK C++ (one codebase)
   │ transport: libcurl             │ transport: emscripten_fetch
   └───────────────┬────────────────┘
                   │  REST/JSON over HTTP           (WebSocket arrives at L2)
                   ▼
          ┌─────────────────────┐
          │  Gateway (Drogon)   │  ApiKeyFilter → project   AuthFilter → user (JWT)
          └──────────┬──────────┘
         ┌───────────┼────────────┐
         ▼           ▼            ▼
   AuthController  LbController   (future services plug in here)
         │           │
         ▼           ▼   service layer (unit-testable logic)
   AuthService    LeaderboardService
         └───────────┼────────────┘
                     ▼  drogon::orm::DbClient (parameterized SQL)
           SQLite (dev + this slice)  │  Postgres (prod, later)
```

The **iron boundary** from `requirements.md` §11 still holds, verbatim: the backend
is a **separate process and build target**. Drogon, libsodium, and the SQL layer
live *only* under `baas/`. The engine core and the games gain **no** new dependency.
The one thing that touches both worlds — the SDK — is its own small library
(`gbaas_sdk`) whose only native dependency is libcurl. You could delete `baas/` and
every game still builds and runs; the backend is a service the game *calls*, never a
thing it *links*.

## 4. Why Drogon, and why a modular monolith

**Drogon** (a modern C++17 async web framework) is named as an example in §11, and it
gives us for free exactly what hand-writing would cost months: a non-blocking event
loop, HTTP **and** WebSocket, and a database client with parameterized queries. We
compared it against Crow (no ORM) and against upgrading our Chapter-33 server (which
is *blocking, one connection at a time* — it cannot host a single realtime service).
For "must be real and demoable," Drogon wins; see the spec's decision table.

We run the services as a **modular monolith**, not microservices. The vision says
"independent services," and that is right *at scale* — but splitting into separate
processes now would buy network hops, service discovery, and deployment complexity
with zero load to justify them. Instead each service is a **module with a clean
interface** (`Controller` = the HTTP edge, `Service` = the logic) inside one binary.
The seams are placed so a service can later be lifted into its own process — expected
first at L2, when realtime traffic actually needs separate scaling. *Choose the
higher rung that holds: a monolith with seams, not premature microservices.*

## 5. The transport seam — the engine's platform seam, again

Here is the idea worth internalizing, because it is the same one that made the engine
web-portable. The engine isolates SDL behind `platform.hpp`, with a desktop backend
(`backend_sdl.cpp`) and a web backend (`backend_web.cpp`). The SDK does the *exact*
same thing for HTTP:

```
gbaas::ITransport            (interface: async request → callback)
   ├── HttpTransportCurl        (native; libcurl)
   └── HttpTransportEmscripten  (web;    emscripten_fetch)
```

Because the colony game builds both native and WASM, the SDK must run in both. The
rest of the SDK (auth, leaderboard, JSON, token handling) is transport-agnostic and
unit-tested against a *fake* transport. Two more consequences of the engine's DNA
carry over: the SDK is **non-blocking** (results are delivered when you pump
`Client::update()` from the game tick — never a blocking network call above the
loop), and its wire format is plain **REST/JSON** so `curl` can debug it and the
Unity/Unreal SDKs (later) only have to speak the same protocol, not re-implement it.

## 6. Where each piece lives

```
baas/                         the backend process (Drogon) — chapters 52–55
  main.cc  app_config.*  app_setup.*
  common/   errors, request-attribute keys
  gateway/  api_key_filter, auth_filter
  auth/     password (argon2id), jwt (HS256), auth_service, auth_controller
  leaderboard/  lb_service, lb_controller
  db/       db (DbClient factory, migration, seed)
sdk/cpp/                       the unified C++ SDK — chapters 56–57
tests/    test_baas_jwt, test_baas_auth, test_baas_leaderboard, baas_test_util.h
```

Read on: **Chapter 52** builds the Drogon app and the Gateway; **53** does
authentication; **54** the database and multi-tenancy; **55** the leaderboard; then
**56–57** the SDK and the colony integration, and **58** the end-to-end acceptance.

## 7. Checkpoints

- Explain, in one sentence each, why the backend is a *separate process* and why it
  is a *monolith* rather than microservices — and why those two facts don't conflict.
- The SDK's transport seam mirrors which engine file? Name the two backends of each.
- Which parts of this system did we hand-write, and which did we delegate to Drogon —
  and what was the deciding principle?
