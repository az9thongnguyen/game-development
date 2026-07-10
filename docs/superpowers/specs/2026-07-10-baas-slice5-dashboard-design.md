# Game BaaS — Slice #5: Dashboard + Admin API (design + plan)

- **Date:** 2026-07-10 · **Status:** Approved (autonomous) · **Builds on:** Slices #1–#4.

## 1. Goal

The platform's **operator half** and its public face — the differentiator from the
original brief: *"create a project on a dashboard, toggle/configure services, and use
it immediately via the SDK."* This slice adds the **admin API** (the write/query side
of the L1 services) and a hand-written **web dashboard** (vanilla HTML/JS, served by
the baas) that drives it. No game-SDK changes.

## 2. Auth model (two levels)

- **Platform admin** — a single secret (`BAAS_ADMIN_SECRET`, env/CLI, dev default with
  a warning), sent as `X-Admin-Secret`. Gates *project creation + listing* (there is no
  project yet, so no per-project key exists). Verified by `AdminSecretFilter`.
- **Project admin** — the project's **secret key** (`X-Secret-Key`), verified (argon2)
  against `projects.secret_key_hash` for the project resolved from `X-Api-Key`. Gates
  per-project admin. Verified by `SecretKeyFilter` (runs after `ApiKeyFilter`).

Seed now stores a **real** secret key (`sk_demo_colony`, hashed) and prints it.

## 3. Admin API (`/v1/admin/*`)

Platform-admin (`X-Admin-Secret`):
- `POST /v1/admin/projects` `{name}` → `{id, name, public_key, secret_key}` (secret shown once).
- `GET  /v1/admin/projects` → `{projects:[{id,name,public_key}]}`.

Project-admin (`X-Api-Key` + `X-Secret-Key`):
- `PUT /v1/admin/config/{key}` `{value}` → `{key,value}` · `DELETE /v1/admin/config/{key}`.
- `POST /v1/admin/events` `{key,name,starts_at,ends_at,payload}` → `{key}` (schedule a live event).
- `GET  /v1/admin/analytics/summary` → `{counts:[{name,count}]}`.
- `GET  /v1/admin/users` → `{users:[{id,email,display_name,is_guest}]}`.
- `GET  /v1/admin/leaderboards/{key}/top?limit=N` → reuse leaderboard top (admin view).

Writes reuse/extend the existing services (config set/delete, live create, analytics
summary) so business logic stays in one place. All project-admin queries are scoped by
`project_id`.

## 4. Dashboard (hand-written SPA, served by baas)

A single `baas/web/dashboard.html` (vanilla JS, no framework, no external assets),
served at `/dashboard` via `newFileResponse` (path from `--dashboard`, default
`baas/web/dashboard.html`). Two panels:
1. **Platform** — enter admin secret → create project (shows public/secret keys once) / list projects.
2. **Project console** — paste a project's public + secret key → tabs: **Config**
   (list + set/delete), **Live Events** (list + schedule), **Analytics** (name→count),
   **Users** (list), **Leaderboard** (top). Calls the admin API with `fetch`.

Keys are held in the page only (localStorage), never logged. Same-origin (served by the
baas) so no CORS.

## 5. Build order

- **S5.1** admin auth: `AdminSecretFilter` + `SecretKeyFilter`; real seeded secret;
  `BAAS_ADMIN_SECRET` in app_config.
- **S5.2** admin service/controllers (projects create/list; config set/delete; event
  create; analytics summary; users list) — extend existing services where natural.
- **S5.3** `dashboard.html` + `/dashboard` route + `--dashboard` arg.
- **S5.4** integration test `baas_admin` (auth gating, create project→use it, config
  write→read via the public API, event schedule→appears active, analytics summary,
  users list, cross-project rejection).
- **S5.5** acceptance: guidebook ch.62, overview/README, security grep, ASan/UBSan,
  browser smoke of the dashboard, merge.

## 6. Security

Admin writes require a verified secret (argon2, constant-time). Platform-admin secret
never committed (env/CLI, gitignored config). Per-project admin scoped by project;
`SecretKeyFilter` rejects a secret that doesn't match the api-key's project. Secrets
shown once (project creation), stored hashed, never returned again or logged. Same
parameterized-SQL + validation posture as L1.
