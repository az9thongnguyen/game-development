# Chapter 58 — Slice #1 Acceptance

> **What this is.** The finish line for the first BaaS slice: the end-to-end
> picture, the security checklist we actually hold to, how the tests prove
> behavior (not just compilation), and an honest list of what's deliberately left
> for later slices. If Chapters 51–57 built it, this chapter is the sign-off.

---

## 1. What ships

A real, demoable vertical of a game backend:

```
colony game ─▶ gbaas C++ SDK ─▶ Gateway (Drogon) ─▶ Auth ─▶ Leaderboard ─▶ SQLite ─▶ back
   native (libcurl) / web (emscripten_fetch)      api-key      argon2id+JWT   best-keep
```

- **Auth:** register, login, and **guest** accounts; argon2id passwords; HS256 JWT.
- **Leaderboard:** authenticated submit (keep-best), public top-N, "my rank".
- **Multi-tenant:** every request is scoped to a project by its api key.
- **SDK:** one non-blocking C++ client, native and WASM, driving the colony demo.

Run it: `baas --db sqlite://baas.db --seed` then `baas --db … --static build-web`,
and `./build/demo --colony` (native) or `http://127.0.0.1:8080/demo.html?mode=colony`
(web).

## 2. Security checklist (held, not hoped)

| Concern | How it's handled | Proven by |
|---|---|---|
| Passwords | argon2id (libsodium); never plaintext, never returned | `baas_jwt` |
| Tokens | HS256 over libsodium HMAC; constant-time verify; short exp | `baas_jwt` |
| SQL injection | parameterized queries only; operators/keywords are fixed literals | code + review |
| Tenant isolation | every query filtered by `project_id` | `baas_leaderboard` |
| Score spoofing | writer taken from the JWT, not the request body | `baas_leaderboard` |
| User enumeration | identical 401 for bad-password and unknown-email | `baas_auth` |
| Input bounds | value range-checked; `limit` clamped; body validated | `baas_leaderboard` |
| Secrets | JWT secret + DB creds from env/CLI; gitignored | config |

## 3. Tests — behavior, not "it compiles"

Six suites, all in CTest, run under ASan/UBSan:

- **`sdk_client`** — SDK logic against a fake transport (assembly, token, parse, errors).
- **`baas_jwt`** — argon2id + JWT unit tests (round-trip, tamper, expiry).
- **`baas_auth`** — the gateway + register/login/guest matrix incl. no-enumeration.
- **`baas_leaderboard`** — ranking, best-keep, anti-spoof, cross-tenant isolation, bounds.
- **`sdk_live`** — the real libcurl transport against a live server, end-to-end.
- The web path is verified in a browser: the WASM build's `emscripten_fetch` reaches
  `POST /v1/auth/guest` → 200.

The integration suites boot the actual Drogon app on an ephemeral port against a temp
SQLite DB and drive it with libcurl — a real server, not a mock.

## 4. Decisions worth remembering

- **Framework for transport, hand-write the logic.** Drogon owns sockets/routing/DB;
  we own the architecture, the service logic, the tenancy model, and the SDK.
- **Modular monolith with seams**, not microservices — split later when load demands.
- **SQLite now, Postgres later** — same `DbClient`, portable SQL; the Homebrew Drogon
  bottle lacks libpq, so Postgres is a documented source-build step.
- **JWT over libsodium**, no `jwt-cpp` — one fewer dependency, crypto stays audited.
- **The transport seam** is the engine's platform seam applied to HTTP; it's what lets
  one SDK serve native and web.

## 5. Deliberately out of scope (future slices)

Cloud Save, Inventory, Remote Config, Analytics, Live Events (L1); Lobby, Matchmaking
+ WebSockets (L2); the project **dashboard** UI (L3); dedicated hosting, voice,
anti-cheat, replay, cross-platform login, telemetry, AI services (L4); Unity/Unreal
SDKs; TLS-in-app, refresh tokens, email verification, password reset, guest→account
upgrade, per-IP rate limiting, an admin API, and runtime Postgres. Each is named so
the architecture leaves room for it; none is pretended to exist.

## 6. Where it plugs in next

Every L1 service (Cloud Save first) is the same shape as the leaderboard: a
`Service` + `Controller` behind the gateway, a table scoped by `project_id`, an SDK
handle, and a test suite that boots the real app. The spine built here — gateway,
auth, tenancy, DB, SDK — is done once. That was the whole point of a walking skeleton.

## 7. Checkpoints

- Pick three rows of the security table and explain the attack each prevents.
- Which test would fail first if someone dropped a `project_id` clause from a query?
- Describe the steps to add "Cloud Save" as the next service, reusing this spine.
