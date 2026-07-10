# Chapter 62 — The Dashboard & Admin API

> **What this is.** The platform's **operator half** and its public face — the
> differentiator from the original brief: *create a project on a dashboard,
> configure services, and use them immediately via the SDK.* This slice adds the
> **admin API** (the write/query side of the L1 services) with a **two-level auth
> model**, and a hand-written **web dashboard** (vanilla JS, served by the baas)
> that drives it. Code: `baas/admin/*`, `baas/gateway/admin_filters.*`,
> `baas/web/dashboard.html`.

---

## 1. The L1/L3 split, realized

Chapter 61 drew a line: each service has a **game-facing** half (L1: read/ingest,
via the SDK) and an **operator** half (L3: write/query, via a dashboard). L1 built
the first; this slice builds the second. So the admin API mostly *reuses the existing
services* — `cfg::set/remove`, `live::create`, `analytics::summary` — behind admin
auth, plus project provisioning and user listing. Business logic stays in one place;
the admin layer is just a differently-authenticated door to it.

## 2. Two levels of admin auth

The operator and the game authenticate differently, so there are two admin gates:

- **Platform admin** — a single secret (`BAAS_ADMIN_SECRET`), sent as `X-Admin-Secret`,
  checked by `AdminSecretFilter`. It gates *project creation and listing* — operations
  that exist *before any project does*, so no per-project key could authorize them.
  The compare is constant-time (`sodium_memcmp`).
- **Project admin** — the project's **secret key**, sent as `X-Secret-Key`, verified
  (argon2, via `pw::verify`) against `projects.secret_key_hash` by `SecretKeyFilter`.
  It runs *after* `ApiKeyFilter`, so it verifies the secret against the project the
  public key resolved to. A secret from project B presented with project A's api key
  is rejected — a test asserts exactly that cross-project rejection.

This is the same layered-filter idea as the gateway: `ApiKeyFilter` → project, then a
second filter adds authority.

## 3. Provisioning: keys minted once

`POST /v1/admin/projects` mints a project with a random **public key** (`pk_…`) and
**secret key** (`sk_…`), both from libsodium's `randombytes_buf`. The secret is
returned **once**, in that response, and stored only as an argon2 hash — it can never
be read back, only verified. This is the same discipline as passwords: the server
holds a verifier, not the secret. (The seed does the same for the demo project;
`--seed` prints `sk_demo_colony` so you can drive the dashboard immediately.)

## 4. The admin API

Platform (`X-Admin-Secret`): `POST /v1/admin/projects`, `GET /v1/admin/projects`.
Project (`X-Api-Key` + `X-Secret-Key`): `PUT/DELETE /v1/admin/config/{key}`,
`POST /v1/admin/events`, `GET /v1/admin/analytics/summary`, `GET /v1/admin/users`.
The dashboard reads game-facing data through the *public* endpoints it already has
(`GET /v1/config`, `/v1/events`, `/v1/leaderboards/{key}/top`) with just the api key —
no need to duplicate reads behind admin auth.

`test_baas_admin` exercises the whole surface: admin-secret gating, create→list,
per-project secret gating, cross-project rejection, config set→public-read→delete,
event schedule→appears active, analytics summary, users list.

## 5. The dashboard — hand-written, served by the baas

`baas/web/dashboard.html` is one page of vanilla JS (no framework, no external
assets, ~200 lines), served at `/dashboard` via `newFileResponse`. Two panels:

1. **Platform admin** — enter the admin secret; create a project (the keys are shown
   once, in the page) or list projects.
2. **Project console** — paste a project's public + secret keys (kept in
   `localStorage`, never logged); tabs for **Config** (list/set/delete), **Live
   Events** (list active / schedule), **Analytics** (name→count), **Users**, and
   **Leaderboard**. Every action is a `fetch` to the admin or public API.

Because the baas serves the page, it's **same-origin** with the API — no CORS, the
base URL is empty, calls just work (the same trick the colony web build used). A
`try/catch` around each `fetch` surfaces errors inline (the first load shows *"missing
X-Api-Key"* until you paste keys — graceful, not a crash).

## 6. Why this is the differentiator

Every prior slice made the platform more capable; this one makes it **usable by
someone who isn't editing C++**. An operator creates a project, copies the public key
into their game's SDK config, flips a remote-config value or schedules an event, and
watches analytics — all from a browser, all against the same backend the game talks
to. That loop — *dashboard configures, SDK consumes, same project* — is the whole
premise from the first brainstorm, now real.

## 7. Checkpoints

- Why does project *creation* need a different auth level than setting a project's
  config? Which filter guards each, and what does each compare?
- The project secret key is returned once and stored hashed. What does that buy you,
  and what's the cost to the operator?
- The dashboard reads leaderboard/config data via the *public* endpoints but writes
  via `/v1/admin/*`. Why split it that way instead of admin-gating the reads too?
