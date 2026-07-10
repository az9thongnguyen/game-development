# Game BaaS — Slice #6: Realtime (Lobby + Matchmaking over WebSocket) — design + plan

- **Date:** 2026-07-10 · **Status:** Approved (autonomous) · **Builds on:** Slices #1–#5.
- **Tier:** L2 (realtime) — the last hand-buildable tier. L4 (voice, dedicated
  hosting, anti-cheat, replay, cross-platform login, telemetry, AI) stays out of
  scope: it needs real cloud / third-party infrastructure (see Slice #1 spec §16).

## 1. Goal

Add the platform's **realtime half**: a persistent, bidirectional channel so a
game can put players into shared **rooms** (Lobby) and **auto-pair** waiting
players into matches (Matchmaking). Everything up to now was request/response
(REST); this slice adds the push channel the earlier tiers structurally lacked.

Two player-facing capabilities:

- **Lobby** — join a named room, leave it, and broadcast small messages to
  everyone else in the same room (chat, ready-checks, presence).
- **Matchmaking** — enter a per-project queue; when ≥2 players wait, the server
  pops a pair, creates a fresh match room, joins both, and pushes `matched` to
  each. (FIFO, pair-of-two — the smallest useful matcher; skill/party matching
  is a documented extension.)

## 2. Why WebSocket, and where the logic lives

REST can't push. A leaderboard client polls; a lobby peer must be *told* when
someone joins. WebSocket is one TCP connection upgraded to full-duplex framed
messages — exactly the seam a lobby needs. We stand on the framework's transport
(Drogon's `WebSocketController` server-side; the browser's native `WebSocket` and
libcurl's `ws://` client-side) and **hand-write all the game-backend logic**: the
hub, room membership, the matcher, tenant isolation, and disconnect cleanup. That
is the learning + product substance; the byte-level WS framing is plumbing we
reuse, exactly as the REST tier reused Drogon's HTTP parser.

## 3. Architecture

```
                       ws://host/v1/ws?api_key=..&token=..
  ┌──────────┐   upgrade + auth (query params)   ┌────────────────────────────┐
  │  client  │◄─────────────────────────────────►│  WsController (Drogon)      │
  │ (browser │      JSON text frames             │   handleNewConnection  →auth│
  │  / SDK)  │                                    │   handleNewMessage →dispatch│
  └──────────┘                                    │   handleConnectionClosed    │
                                                  └──────────────┬─────────────┘
                                                                 │ (thread-safe)
                                                  ┌──────────────▼─────────────┐
                                                  │  RealtimeHub  (singleton)   │
                                                  │   mutex-guarded in-memory:  │
                                                  │   rooms_  : {proj:room → set<conn>} │
                                                  │   queue_  : {proj → [conn]}         │
                                                  │   join/leave/msg/enqueue/cancel/    │
                                                  │   on_disconnect                     │
                                                  └─────────────────────────────┘
```

- **Transport is separate from the process model.** The hub is in-memory and
  process-local — correct for a single-node demo. `// ponytail: single-node
  in-memory hub; a multi-node deploy needs a shared bus (Redis pub/sub) — the
  hub interface is the seam to swap.` Stated, not built.
- **State lives in `RealtimeHub`, not the controller.** The controller is a thin
  adapter (parse frame → call hub); the hub owns all shared state behind one
  `std::mutex`. Drogon runs multiple event-loop threads, so every hub method
  locks. `// ponytail: one coarse mutex; the hub does only map ops + small sends,
  so contention is negligible at demo scale — shard by project if it ever isn't.`

## 4. Authentication (on the upgrade)

A WebSocket can't carry per-message headers, so auth happens **once, on the
upgrade request**, via query params (the same two credentials the REST tier uses):

- `api_key` → `SELECT id FROM projects WHERE public_key=?` → `project_id`
  (same lookup as `ApiKeyFilter`).
- `token` → `jwt::verify(token, secret)` → `Claims{sub=user_id, pid}`; require
  `pid == project_id` (same check as `AuthFilter`).

On failure, send one `{"ev":"error","message":".."}` frame and close the socket
immediately (`conn->shutdown()`), before any hub state is created. On success,
attach a per-connection `ConnMeta{project_id, user_id, display_name, room}` via
`conn->setContext`. Filters can't run on WS upgrades in Drogon the way they do on
HTTP, so this check is done inline in `handleNewConnection` — reusing the exact
same query + verify calls so the security posture is identical.

## 5. Wire protocol (JSON text frames)

Client → server (`op`):

| frame | meaning |
|---|---|
| `{"op":"join","room":"lobby-1"}` | leave any current room, join `room` |
| `{"op":"leave"}` | leave current room |
| `{"op":"msg","data":"..."}` | broadcast `data` (opaque string) to room peers |
| `{"op":"queue"}` | enter the matchmaking queue |
| `{"op":"cancel"}` | leave the queue |

Server → client (`ev`):

| frame | when |
|---|---|
| `{"ev":"joined","room":"..","members":[{"user_id":N,"name":".."}]}` | after your join |
| `{"ev":"peer_joined","user_id":N,"name":".."}` | a peer joined your room |
| `{"ev":"peer_left","user_id":N}` | a peer left / disconnected |
| `{"ev":"msg","from":N,"name":"..","data":".."}` | a peer broadcast |
| `{"ev":"matched","room":"match_K"}` | the matcher paired you (you're now in that room) |
| `{"ev":"error","message":".."}` | bad frame / auth failure |

Room membership is **project-scoped**: the hub keys rooms by `project_id + ":" +
room`, so two tenants using the room name `"lobby"` never see each other. The
matcher's queue is per-`project_id` for the same reason.

## 6. SDK (games use realtime like every other service)

- **`gbaas::Realtime`** handle on `Client`: `connect()`, `join(room)`, `leave()`,
  `send(data)`, `queue()`, `cancel()`, and non-blocking `poll(RtEvent&)` to drain
  received events. Pumped by `Client::update()` (same frame-driven model as REST).
- **WS transport seam** `gbaas::IWsTransport` (mirrors `ITransport`): `open(url)`,
  `close()`, `send_text()`, `poll(std::string&)`, `connected()`.
  - **native** — `WsTransportCurl` over libcurl `ws://` (`CONNECT_ONLY=2` +
    `curl_ws_send`/`curl_ws_recv`, non-blocking). Compiled only when CMake finds a
    **WS-capable libcurl** (`curl/websockets.h` + `CURLWS_*`); otherwise a stub
    that reports "native WebSocket needs libcurl ≥ 7.86 with ws". System curl on
    macOS lacks it, so the SDK build prefers Homebrew's keg-only curl when present.
  - **web** — `WsTransportEmscripten` over `emscripten/websocket.h`
    (`emscripten_websocket_*`), callbacks push frames into a queue. This is the
    natural browser transport and the path the web colony build uses.

## 7. Demo (visible, browser-verifiable)

A **Realtime tab** in the existing dashboard SPA (`baas/web/dashboard.html`),
using the **browser's native `WebSocket`** — zero dependencies, same-origin. It
connects with the console's api-key + a guest token it fetches first, lets the
operator join a room / send a message / queue for a match, and prints the event
stream. This proves the server in a real browser without any native build.

Colony gets a **minimal presence hook** (optional, lazy): connect + join a
`colony` room and show the peer count. `// ponytail: full state replication is
netcode out of scope; presence proves the channel end-to-end.`

## 8. Build order

- **S6.1** `RealtimeHub` (hub.h/.cc): rooms, queue, join/leave/msg/enqueue/cancel/
  on_disconnect, all mutex-guarded, project-scoped; unit-testable pure logic where
  possible.
- **S6.2** `WsController` (ws_controller.h/.cc): `/v1/ws`, upgrade auth, frame
  dispatch, close cleanup; add to `baas_core` OBJECT lib (self-registers).
- **S6.3** integration test `baas_realtime` using Drogon's `WebSocketClient`:
  two clients matchmake→paired; lobby join→broadcast reaches peers; tenant
  isolation (project B invisible to A); auth rejection (bad key/token closes).
- **S6.4** SDK: `IWsTransport` + native (libcurl-ws, gated) + emscripten impls;
  `Realtime` handle; CMake WS-capable-curl detection. Wire a native SDK realtime
  smoke into the integration test when WS-curl is available.
- **S6.5** dashboard Realtime tab (browser WebSocket) + colony presence hook.
- **S6.6** acceptance: guidebook ch.63 (server+hub+matchmaking) & ch.64 (SDK WS +
  demo); overview/README rows; ASan/UBSan; security grep (project scoping);
  browser smoke; merge `--no-ff`.

## 9. Security

- Auth on upgrade reuses the exact api-key lookup + JWT verify + project-match
  check from the REST filters — no weaker path in.
- Every room/queue op is scoped by the connection's authenticated `project_id`
  (from the token, never a client-supplied field) → no cross-tenant leakage and
  no room-name spoofing across projects.
- `msg` payloads are opaque and length-capped (server rejects frames over a small
  cap, e.g. 8 KiB) to bound memory; the server never interprets them.
- Disconnect always runs `on_disconnect` (leave room + drop from queue) so a
  dropped socket can't leave a ghost in a room or a stale queue slot.
- Same parameterized-SQL posture as L1 for the one project lookup.

## 10. Out of scope (documented, not built)

Authoritative game simulation / tick-synced state replication; reconnection with
session resume; skill/party/region matchmaking; persistence of rooms across
restarts; multi-node fan-out (Redis). Each is a named extension point, not a gap.
