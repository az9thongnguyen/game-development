# Chapter 63 — Realtime: Lobby & Matchmaking over WebSocket (server)

> **Where we are.** Slices #1–#5 gave the platform a full *request/response* half:
> auth, leaderboards, cloud save, inventory, config, analytics, live events, and an
> operator dashboard — all over REST. This chapter builds the **realtime half**:
> a persistent, push-capable channel so players can share *rooms* (Lobby) and be
> *auto-paired* into matches (Matchmaking). This is tier **L2**, and it is the last
> tier we can meaningfully hand-build (L4 — voice, dedicated hosting, anti-cheat,
> replay — needs real cloud/third-party infrastructure; see ch.58 §"out of scope").

---

## 1. Why REST is not enough

Every service so far follows the same shape: the client asks, the server answers,
the connection closes. That is perfect for "submit my score" or "load my save",
and it is why the leaderboard is a *poll*: the client re-asks `/top` whenever it
wants fresh data.

A lobby cannot work that way. When player B joins the room player A is in, **A must
be told** — without asking. REST has no way for the server to speak first. You can
fake it with polling ("any news? any news? any news?"), but that is wasteful and
laggy: the news is stale by up to one poll interval, and 100 idle players hammer
the server 100×/second for nothing.

The fix is a connection that stays open and lets **either side** send at any time.
That is a **WebSocket**.

```
   REST (request/response)                WebSocket (full-duplex, persistent)
   ───────────────────────                ──────────────────────────────────
   client ──"GET /top"──▶ server          client ══════════════════▶ server
   client ◀──"[...]"──── server                  ◀════════════════════
   (connection closes)                    (one connection; both sides push,
   client ──"GET /top"──▶ server           for as long as it stays open)
   client ◀──"[...]"──── server            server ──"peer_joined"──▶ client
                                                            (unprompted!)
```

### What a WebSocket actually is

A WebSocket starts life as an ordinary HTTP request with two magic headers
(`Upgrade: websocket` + `Sec-WebSocket-Key`). The server replies `101 Switching
Protocols`, and from that instant the same TCP socket stops speaking HTTP and
starts speaking the **WebSocket framing protocol**: a tiny binary envelope around
each message (opcode = text/binary/ping/pong/close, a length, an optional mask,
then the payload). Both ends may send a frame whenever they like.

We do **not** implement that framing by hand. Drogon's `WebSocketController` does
the upgrade and the frame (de)serialization for us — exactly as its HTTP parser did
for REST. Our job is everything *above* the frame: who is in which room, who is
waiting for a match, and what to do when a socket drops. That is the game-backend
logic, and it is the part worth learning.

> **Design rule restated.** We stand on the framework's transport and hand-write the
> domain logic. The same split that kept SDL confined to `src/platform/` keeps
> Drogon's WebSocket plumbing confined to one thin controller.

---

## 2. The shape of the feature

Two capabilities, one endpoint (`/v1/ws`):

- **Lobby** — `join` a named room, `leave` it, and `msg` (broadcast) to everyone
  else in it. Think chat, ready-checks, presence.
- **Matchmaking** — `queue` to wait for an opponent; when two players wait, the
  server pops a pair, makes a fresh room, puts both in it, and pushes `matched`.
  `cancel` leaves the queue.

Everything is **scoped to a project** (the authenticated tenant): two games that
both use a room called `"lobby"` never see each other, and two players queuing in
different projects never match.

---

## 3. Architecture: a thin controller over a fat hub

Drogon dispatches WebSocket callbacks on **several event-loop threads**. If we
scatter the room/queue state across the controller, every access becomes a data
race. So we concentrate *all* shared state in one place — the **`RealtimeHub`** —
behind **one mutex**, and make the controller a stateless adapter.

```
   ws://host/v1/ws?api_key=..&token=..
        │  (HTTP upgrade → 101)
        ▼
 ┌─────────────────────────────┐        ┌──────────────────────────────────┐
 │ WsController  (per socket)   │        │ RealtimeHub  (one, process-wide)  │
 │  handleNewConnection  ──auth─┼───────▶│  std::mutex mu_                   │
 │  handleNewMessage ──dispatch─┼───────▶│  rooms_ : {"pid:room" → {conns}}  │
 │  handleConnectionClosed  ────┼───────▶│  queue_ : {pid → [conns]}         │
 └─────────────────────────────┘        │  join/leave/msg/enqueue/cancel/   │
        (no shared state here)           │  on_disconnect                    │
                                         └──────────────────────────────────┘
```

- **Controller** = parse a frame, call the right hub method. Holds nothing.
- **Hub** = the truth. Every method takes the mutex, mutates maps, and sends the
  resulting frames. Because it is the *only* writer, there is exactly one lock to
  reason about.

> `// ponytail: single-node, in-memory, one coarse mutex.` Correct for a single
> server. Scaling to many nodes needs a shared bus (Redis pub/sub) so a broadcast
> reaches sockets on other machines; the hub's method surface is the seam where
> that swap happens. Stated in the code, not built.

---

## 4. Authentication happens on the *upgrade*

A subtle but important point: a WebSocket carries **no per-message headers**. You
cannot put `Authorization: Bearer …` on frame #37. So all authentication must be
done **once**, on the upgrade request — the one and only HTTP request in the
socket's life.

We reuse the *exact* two credentials the REST tier uses, passed as query params:

```
ws://host/v1/ws?api_key=<project public key>&token=<the user's JWT>
```

`handleNewConnection` (in `baas/realtime/ws_controller.cc`) does three checks, each
identical to a REST filter:

```cpp
// 1. api_key → project_id   (same query as ApiKeyFilter)
const auto rows = db::client()->execSqlSync(
    "SELECT id FROM projects WHERE public_key=?", api_key);
if (rows.empty()) { send_error(conn, "invalid api_key"); conn->shutdown(); return; }
const long project_id = rows[0]["id"].as<long>();

// 2. token → claims, and it must belong to THIS project   (same as AuthFilter)
const auto claims = jwt::verify(token, config().jwt_secret);
if (!claims || claims->pid != project_id) {
    send_error(conn, "invalid or expired token"); conn->shutdown(); return;
}

// 3. resolve the display name (tenant-scoped) for peer lists
//    SELECT display_name FROM users WHERE id=? AND project_id=?
```

On success we attach a **`ConnMeta`** to the connection with `conn->setContext`:

```cpp
struct ConnMeta {
    long project_id, user_id;
    std::string display_name;
    std::string room;      // "" = not in a room
    bool queued = false;
};
```

From now on, every hub call reads the connection's `ConnMeta` to know *who* and
*which tenant* — never a client-supplied field. That is the anti-spoof guarantee,
same as the leaderboard using the JWT's user instead of a body `user_id`.

> **Why `shutdown()` and not "reject the upgrade"?** By the time
> `handleNewConnection` runs, Drogon has already sent `101 Switching Protocols` —
> the socket is open. So we can't return a 401; instead we send one `error` frame so
> the client knows *why*, then close. (The SDK and the dashboard both surface that
> frame.) This is a real quirk of the framework, worth remembering.

---

## 5. The wire protocol

Plain JSON text frames. Client → server uses an `op`; server → client uses an `ev`.

| client → server           | meaning                                   |
|---------------------------|-------------------------------------------|
| `{"op":"join","room":"r"}`| leave any current room, join `r`          |
| `{"op":"leave"}`          | leave the current room                     |
| `{"op":"msg","data":"…"}` | broadcast `data` to the room (no self-echo)|
| `{"op":"queue"}`          | enter matchmaking                          |
| `{"op":"cancel"}`         | leave matchmaking                          |

| server → client                                              | when                         |
|--------------------------------------------------------------|------------------------------|
| `{"ev":"joined","room":"r","members":[{user_id,name}…]}`     | after *your* join            |
| `{"ev":"peer_joined","user_id":N,"name":"…"}`                | a peer joined your room       |
| `{"ev":"peer_left","user_id":N}`                             | a peer left / disconnected    |
| `{"ev":"msg","from":N,"name":"…","data":"…"}`                | a peer broadcast              |
| `{"ev":"matched","room":"match_K"}`                          | the matcher paired you        |
| `{"ev":"error","message":"…"}`                               | bad frame / auth failure      |

The `data` field is **opaque** to the server: it never parses it. A game can put
its own JSON in there (a chat line, a ready flag, a tiny state delta). The server
only length-caps the whole frame (8 KiB) to bound memory.

---

## 6. Inside the hub

Full source: `baas/realtime/hub.h` / `hub.cc`. The state:

```cpp
std::mutex mu_;
std::map<std::string, std::set<WebSocketConnectionPtr>> rooms_;   // "pid:room" → conns
std::map<long, std::vector<WebSocketConnectionPtr>>     queue_;   // pid → FIFO
long match_counter_ = 0;
```

Two representation choices worth explaining:

- **Rooms are a `set` of connection pointers**, keyed by the string `"pid:room"`.
  The `pid:` prefix is the whole tenant-isolation mechanism — project 1's `"lobby"`
  is the key `"1:lobby"`, project 2's is `"2:lobby"`; different keys, different sets,
  never any crossover. A `set` gives O(log n) insert/erase and dedup for free.
- **The queue is a `vector` per project** — a FIFO. The oldest waiter is at the
  front, so matches are fair (first-come-first-served).

### join — the interesting one

```cpp
void RealtimeHub::add_to_room_locked(conn, ConnMeta& m, const string& room, bool notify) {
    leave_room_locked(conn, m);                 // 1. leave whatever room you were in
    m.room = room;
    auto& members = rooms_[room_key(m.project_id, room)];
    members.insert(conn);                        // 2. join the new one
    if (!notify) return;

    // 3. tell the joiner who's here now (including themselves)
    Json joined = {ev:"joined", room, members:[peer_obj(each)]};
    send_json(conn, joined);

    // 4. tell everyone already here that someone arrived
    Json peer = {ev:"peer_joined", user_id:m.user_id, name:m.display_name};
    for (c : members) if (c != conn) send_json(c, peer);
}
```

Note the ordering: we build the `members` array *after* inserting the joiner, so the
`joined` frame the newcomer receives lists everyone including themselves — a
complete snapshot. The existing peers get a lightweight `peer_joined` delta instead
of a whole new snapshot. Snapshot-for-the-newcomer, delta-for-the-rest is the
classic presence pattern; it keeps the common case (someone joins) cheap.

### msg — broadcast without self-echo

```cpp
void RealtimeHub::broadcast_msg(conn, const string& data) {
    auto m = meta_of(conn);
    if (m->room.empty()) { send_error(conn, "not in a room"); return; }
    Json ev = {ev:"msg", from:m->user_id, name:m->display_name, data};
    for (c : rooms_[key]) if (c != conn) send_json(c, ev);   // skip the sender
}
```

The `if (c != conn)` is deliberate: the sender already knows what they sent, so
echoing it back wastes bandwidth and makes the client dedupe. Our browser smoke
test proves exactly this — sender A's log stays empty of its own `msg`, while B
receives it.

### enqueue — the matcher

```cpp
void RealtimeHub::enqueue(conn) {
    if (m->queued) return;                 // idempotent: queue once
    queue_[m->project_id].push_back(conn);
    m->queued = true;

    auto& q = queue_[m->project_id];
    while (q.size() >= 2) {                 // pair up FIFO, two at a time
        auto a = q[0], b = q[1];
        q.erase(q.begin(), q.begin() + 2);
        string room = "match_" + to_string(++match_counter_);
        add_to_room_locked(a, *meta_of(a), room, /*notify=*/false);
        add_to_room_locked(b, *meta_of(b), room, /*notify=*/false);
        send_json(a, {ev:"matched", room});
        send_json(b, {ev:"matched", room});
    }
}
```

The matcher runs a `while` (not an `if`) so that if several players are already
waiting when one more arrives, it drains as many pairs as it can. We put both
players in the match room with `notify=false` — for a *match*, the meaningful signal
is `matched`, not the lobby's `joined`/`peer_joined` chatter — but they are now real
roommates, so a subsequent `msg` broadcasts between them normally.

`match_counter_` is a monotonically increasing integer under the lock, so every
match room name is unique for the life of the process.

### on_disconnect — the cleanup that prevents ghosts

```cpp
void RealtimeHub::on_disconnect(conn) {
    dequeue_locked(conn, *m);      // drop from the queue if waiting
    leave_room_locked(conn, *m);   // leave the room, tell peers peer_left
}
```

`handleConnectionClosed` always calls this. Without it, a player who closes their
laptop lid would linger forever in a room's `set` (a "ghost" that peers still see)
or hold a stale slot in the queue (so the *next* player matches with a dead socket).
Drogon guarantees `handleConnectionClosed` fires for every closed socket, so this is
airtight.

---

## 7. Concurrency: one lock, and why it's enough

Every public hub method is `std::lock_guard<std::mutex> lk(mu_);` at the top. That
serializes all room/queue mutation. Could this be a bottleneck? Each critical
section does a handful of map operations and a few `conn->send()` calls (which
themselves just enqueue bytes on the connection's own buffer — non-blocking). At
demo/single-node scale, contention is negligible.

> `// ponytail: one coarse mutex; shard by project if it ever isn't.` The upgrade
> path is a `std::map<long, std::mutex>` keyed by project so tenants don't contend —
> but that is speculative until a profiler says otherwise, so we don't build it.

One thing we are careful about: we hold the lock **while sending**. Is that safe?
Yes — `conn->send()` does not call back into the hub, so there is no re-entrancy and
no lock-ordering hazard. If sends could block or re-enter, we'd snapshot the
recipient list under the lock and send after releasing it.

---

## 8. Worked example: two players, one lobby

Trace the browser smoke test (§ verified live), users 2 (A) and 3 (B), project 1:

```
A connects  → ConnMeta{pid=1,uid=2,room=""}
B connects  → ConnMeta{pid=1,uid=3,room=""}

A: {"op":"join","room":"demo"}
   rooms_["1:demo"] = {A}
   → A gets {"ev":"joined","room":"demo","members":[{uid:2}]}

B: {"op":"join","room":"demo"}
   rooms_["1:demo"] = {A,B}
   → B gets {"ev":"joined","members":[{uid:2},{uid:3}]}   (full snapshot)
   → A gets {"ev":"peer_joined","user_id":3}              (delta)

A: {"op":"msg","data":"hello from A"}
   → B gets {"ev":"msg","from":2,"data":"hello from A"}
   → A gets NOTHING (no self-echo)
```

That is exactly the JSON our live browser test returned. If you ran it with a third
player C in project 2 joining `"demo"`, C's key would be `"2:demo"` — a different
set — so none of A/B's frames would reach C. Tenant isolation, for free, from the
key prefix.

---

## 9. Testing it: `sdk_realtime_live`

The realtime tier is covered end-to-end by **`sdk_realtime_live`**, which drives the
server with the **SDK's own native ws:// transport** (ch.64) from a worker thread
while the app runs on the main thread. That one test exercises the server *and* the
real client transport together, and it asserts the whole server surface:

- **auth rejection** — a bad token on the upgrade is refused (a raw libcurl ws
  connection receives an `error` frame / is closed — the 101 still happens first,
  see §4);
- **lobby** — join → `joined` with the right member count; a second join →
  `peer_joined` to the first; broadcast → reaches the peer, **not** the sender;
- **tenant isolation** — a project-B client joins `"lobby"` and sees only itself;
  project A's broadcast never crosses into B;
- **matchmaking** — two waiters both get `matched` with the *same* room;
- **cancel / disconnect** — leaving the queue / dropping the socket cleans up.

> **Why not Drogon's `WebSocketClient`?** An earlier version of this test used
> Drogon's client on a *separate* `trantor::EventLoopThread`. It passed its
> assertions every time but **flaked at teardown** (~1 run in 10): tearing down a
> second event loop while the server's loop is also shutting down raced inside
> trantor ("mutex lock failed" during the quit-time functor drain). The lesson is a
> general one — *coordinating the shutdown of two event loops in one process is
> genuinely hard* — so the test was rebuilt on the SDK's blocking-curl transport,
> which needs no client event loop and is therefore deterministic (0 failures in
> 20+ stress runs). Fewer moving parts at teardown beats a clever harness.

All green under CTest and clean under ASan/UBSan. The Realtime *logic* on the client
side is additionally unit-tested with a fake transport (`sdk_realtime`, ch.64 §6).

---

## 10. Pitfalls (things that bit, or would have)

- **Dangling reference over a parsed temporary.** Iterating
  `for (auto& e : parse(body)["events"])` binds a reference into the temporary that
  `parse()` returned, which dies before the loop. Always bind
  `const auto j = parse(body);` first. (This bug caused *vacuous passes* in an
  earlier slice — the loop ran zero times and every assertion inside was skipped.)
- **Storing state in the controller.** Tempting, and an instant data race across
  Drogon's loop threads. All shared state lives in the hub, behind the lock.
- **Forgetting disconnect cleanup.** Ghost members and stale queue slots. Route
  every close through `on_disconnect`.
- **Trusting a client-supplied identity.** The user is always the JWT's `sub` from
  `ConnMeta`, never a field in the frame — same posture as the leaderboard.
- **Un-capped frames.** Reject frames over a small cap (8 KiB) so a malicious client
  can't make you buffer megabytes.
- **Leaking empty rooms.** `leave_room_locked` erases a room from `rooms_` when its
  set empties, so a busy server doesn't accumulate dead keys forever.

---

## 11. Glossary

- **WebSocket** — a persistent, full-duplex message channel that begins as an HTTP
  upgrade and then speaks a light binary framing protocol; either side may send.
- **Upgrade** — the HTTP request (`Upgrade: websocket`) whose `101` response flips
  the connection from HTTP to WebSocket. The only place we can authenticate.
- **Hub** — the process-wide, mutex-guarded owner of all realtime state.
- **Room** — a named set of connections within one project; broadcasts go to a room.
- **Matchmaking queue** — a per-project FIFO of connections waiting to be paired.
- **Presence** — knowing who is currently connected/in a room (the `joined` /
  `peer_joined` / `peer_left` events).
- **Tenant isolation** — the guarantee that one project can never observe another's
  rooms/queues; here, achieved by the `"pid:room"` key prefix.

---

## 12. Exercises

1. **Room roster query.** Add an `op:"who"` that replies with the current member
   list of your room (a `roster` event). *Hint:* it's `join`'s member-building code
   without the mutation.
2. **Max room size.** Reject a `join` when the room already has N members
   (`{"ev":"error","message":"room full"}`). Where must the check sit to be
   race-free? *Hint:* inside `add_to_room_locked`, under the lock.
3. **Party matchmaking.** Let `queue` carry a `size` so 2v2 needs four waiters.
   *Hint:* change the `while (q.size() >= 2)` threshold and pop `2*size`.
4. **Idle timeout.** Disconnect a socket that has sent nothing for 60s. *Hint:*
   Drogon connections expose `setPingMessage`/timeouts; or stamp `ConnMeta` on each
   frame and sweep from a timer.
5. **Reconnect + resume.** Design (don't build) how a dropped player could rejoin
   their match room within 10s. What state must outlive the socket, and where would
   it live given §3's single-node caveat?

---

*Next: ch.64 — the SDK's realtime channel (native `ws://` via libcurl, web via the
browser WebSocket) and the dashboard's live Realtime console.*
