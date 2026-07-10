# Game BaaS — Slice #4: Remote Config + Analytics + Live Events (design + plan)

- **Date:** 2026-07-10 · **Status:** Approved (autonomous) · **Builds on:** Slices #1–#3.

## 1. Goal

Complete the **L1 tier** by adding the three remaining client-facing services. They
share one shape and one principle: **the game-facing half (read/ingest) is L1; the
operator half (write/query) is the L3 dashboard's job.** So this slice ships read-only
Remote Config, ingest-only Analytics, and read-only Live Events; seeding stands in for
the not-yet-built admin writes.

## 2. Services

### Remote Config (`web::cfg`, `baas/remote_config/`)
Server-controlled key/values a game reads at startup (flags, tunables, MOTD).
- `GET /v1/config` (api-key) → `{config:{k:v,…}}`; `GET /v1/config/{key}` → `{key,value}` / 404.
- Table `config(project_id, key, value)` unique(project_id,key). Seeded: `motd`, `max_agents`.
- SDK `client.config().all()/get(key)`.

### Analytics (`web::analytics`, `baas/analytics/`)
Fire-and-forget gameplay event ingest.
- `POST /v1/analytics/events` (api-key; **no JWT** — events may be anonymous) `{name, props?}`
  → `{ok:true}`. If a valid Bearer is present, attribute to that user (best-effort);
  else `user_id` NULL. `name` whitelist `[A-Za-z0-9_.-]`≤64; `props` opaque JSON ≤4 KiB.
- Table `analytics_events(project_id, user_id NULL, name, props, created_at)`.
- SDK `client.analytics().track(name, props="{}", cb=null)`.

### Live Events (`web::live`, `baas/live_events/`)
Time-boxed server-driven events; the **server clock** decides what's active.
- `GET /v1/events` (api-key) → `{events:[{key,name,payload}]}` where
  `starts_at <= now <= ends_at`.
- Table `live_events(project_id, key, name, starts_at, ends_at, payload)` unique(project_id,key).
  Seeded: an always-active "Double Wood Weekend".
- SDK `client.events().active()`.

## 3. Build order

- **S4.1** three tables appended to the idempotent migration + seed defaults.
- **S4.2** three services (`cfg`, `analytics`, `live`) + three controllers behind the
  gateway (config/events: api-key; analytics: api-key + optional token).
- **S4.3** integration tests `baas_config` / `baas_analytics` / `baas_events`
  (isolation, validation, attribution, event expiry).
- **S4.4** SDK handles `config()/analytics()/events()` + unit (fake) + `sdk_live`.
- **S4.5** colony: banner shows `motd` + active event; submit fires an analytics event.
- **S4.6** acceptance: guidebook ch.61, overview/README, security grep, ASan/UBSan, merge.

## 4. Security

Every query scoped by `project_id` (isolation tests). Parameterized SQL. Analytics is
anonymous-capable but still project-gated; attribution only from a *verified* token
whose `pid` matches the request project. Input whitelists + size caps. Admin
write/query intentionally absent (L3). Same reviewed posture as Slices #1–#3.
