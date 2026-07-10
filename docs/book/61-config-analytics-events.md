# Chapter 61 — Remote Config, Analytics & Live Events

> **What this is.** The last three L1 services, together — because they share one
> shape: **client-facing read/ingest now, admin write/query later (the dashboard,
> L3)**. Remote Config lets the server tune the game without a redeploy; Analytics
> ingests gameplay events; Live Events surface time-boxed happenings. Each is one
> more table + service + controller + SDK handle on the same spine. Code:
> `baas/remote_config/*`, `baas/analytics/*`, `baas/live_events/*`, SDK
> `client.config()/analytics()/events()`.

---

## 1. The dividing line: L1 reads, L3 writes

A useful principle emerges here. These services have two audiences:

- the **game** (via the SDK) — reads config, reads active events, *ingests* analytics;
- the **operator** (via a dashboard) — *sets* config, *schedules* events, *queries*
  analytics.

Slice #4 builds the **game-facing** half (L1). The operator half is the dashboard's
job (L3), where an admin API and secret-key auth naturally live. So Remote Config and
Live Events are **read-only** to the client, and Analytics is **ingest-only** — the
values are seeded now and will be editable from the dashboard later. Splitting a
service by audience keeps each half small and its auth model clean.

## 2. Remote Config — tune without redeploying

Server-controlled key/values a game reads at startup: feature flags, tunables, a
message-of-the-day. Two public reads (api-key, no user — config isn't per-user):

```
GET /v1/config          → {"config": {"motd": "...", "max_agents": "50"}}
GET /v1/config/{key}    → {"key": "...", "value": "..."}   (404 if absent)
```

The service is four lines of parameterized SQL scoped by `project_id`. The point
isn't the code — it's that changing `motd` in the database (later: in the dashboard)
changes every client's behavior with no new build. The namespace is `web::cfg`, not
`web::config`, to avoid colliding with the app-config accessor `web::config()` — a
small reminder that names live in a shared space.

## 3. Analytics — fire-and-forget ingest

Games emit events; the server records them and moves on:

```
POST /v1/analytics/events   {"name": "score.submitted", "props": {...}?}   → {"ok": true}
```

Two design choices worth noting:

- **Anonymous-friendly.** The route is api-key gated but *not* JWT gated — games send
  events before a player logs in (app opens, tutorial steps). If a valid Bearer token
  *is* present, the event is attributed to that user (best-effort); otherwise
  `user_id` is stored `NULL`. The controller does the optional token check itself
  rather than forcing `AuthFilter`.
- **Opaque props, capped.** `props` is stored as an opaque JSON string (≤ 4 KiB) — the
  server doesn't interpret it. Querying/aggregation is the dashboard's job; ingest
  stays cheap.

The SDK's `track()` is fire-and-forget — the callback is optional:

```cpp
client.analytics().track("score.submitted");                 // no callback
client.analytics().track("level.up", R"({"lvl":3})", cb);    // with props + callback
```

Note the SDK embeds `props` as **raw JSON** (not a string), so `{"lvl":3}` arrives as
an object, not a quoted string.

## 4. Live Events — what's happening right now

Time-boxed, server-driven happenings (a sale, a double-XP weekend). The client asks
for the ones **active now**, and the *server's* clock decides:

```sql
SELECT key, name, payload FROM live_events
 WHERE project_id=? AND starts_at <= CURRENT_TIMESTAMP AND ends_at >= CURRENT_TIMESTAMP
```

```
GET /v1/events   → {"events": [{"key": "double_wood", "name": "Double Wood Weekend", "payload": "..."}]}
```

The test seeds one always-active event *and* one already-expired event, and asserts
only the active one comes back — the whole value of a *live* event is that the server,
not the client, decides when it's on.

## 5. Colony wiring

On sign-in the colony fetches `motd` (Remote Config) and the active events, and shows
them as a banner: *"Welcome to Colony!   [LIVE: Double Wood Weekend]"*. Submitting a
score fires an analytics event (`score.submitted`). It's a light touch — the SDK
handles and the backend are the substance — but it exercises all three services
natively and in the browser.

## 6. L1 is complete

Six L1 services now stand on one spine: **Leaderboard, Cloud Save, Inventory, Remote
Config, Analytics, Live Events**. Each was the same bounded move (table → service →
controller → SDK handle → tests → chapter), and the repetition *is* the point: the
walking skeleton turned "add a backend feature" into a checklist. The genuinely new
problems are next — **realtime** (L2: persistent WebSocket connections, stateful
sessions) and the **dashboard** (L3: the operator half of every service above, and
the platform's public face).

## 7. Checkpoints

- Each of these three services splits along an L1/L3 line. State the line in one
  sentence, and say which half this slice built.
- Why is the analytics route api-key gated but *not* JWT gated? How does it still
  attribute events to a logged-in user?
- A live event's `ends_at` passes. What changes for the client, and where is that
  decision made?
