# Chapter 65 — Replay System (record, store, play back)

> **Where we are.** The platform now has state (REST: auth, saves, inventory,
> config, analytics, events), an operator dashboard, and realtime (WebSocket lobby +
> matchmaking). This chapter adds a **Replay System** — recording a game session and
> playing it back later. It is the one item from the original Phase-2 wishlist that
> is genuinely hand-buildable: no special infrastructure, just serialization,
> storage, and deterministic playback. (The rest of Phase 2 — voice, dedicated
> hosting, anti-cheat, cross-platform login, AI — needs real cloud / third-party
> systems and stays out of scope.)

---

## 1. What a replay actually is

There are two ways to "replay" a game, and picking the right one is the whole
design decision:

- **State-snapshot replay** — periodically save the *entire game state* (every unit,
  position, hp…) and, to play back, load snapshots in order. Simple to reason about,
  but the data is huge and it can't interpolate between snapshots.
- **Command-stream replay** — save only the *player's commands* (spawn here, move
  there, at these times) and, to play back, **re-run the simulation** feeding it
  those commands at the same moments. Tiny data (a few bytes per action), and it
  reproduces the whole session — *if the simulation is deterministic*.

Real-time strategy games (StarCraft, Age of Empires) use command-stream replays for
exactly this reason: a 30-minute match is a few kilobytes of commands, not gigabytes
of state. We use the same model. A colony replay is a text stream:

```
0:spawn
12:corner
40:spawn
95:reset
```

Each line is `relative-frame : command`. Play it back and the colony re-enacts the
session.

> **The determinism caveat, stated honestly.** Command-stream replay reproduces the
> session *exactly* only if the sim is bit-for-bit deterministic (same float
> results, same iteration order, seeded RNG). Colony's sim is close but not
> guaranteed bit-exact, so our playback faithfully re-issues the *commands* at the
> right frames — the run looks like the original and the commands are exact, even if
> a pathfinding tie breaks differently. That is the honest, useful version; true
> lockstep determinism is a deeper engine property (an exercise below).

---

## 2. Storage model: like cloud save, but many and immutable

A replay is a stored blob — so it is a cousin of cloud save (ch.59). The
differences drive the schema:

| | Cloud save | Replay |
|---|---|---|
| count per user | one per **slot** (overwritten) | **many**, each new |
| identity | caller-chosen slot name | **server-assigned id** |
| mutability | versioned, overwritten | immutable once written |
| listing | by slot | newest-first, with a name label |

So the `replays` table is append-only per user, keyed by an auto id:

```sql
CREATE TABLE IF NOT EXISTS replays (
  id INTEGER PRIMARY KEY,                       -- server-assigned (rowid)
  project_id INTEGER NOT NULL REFERENCES projects(id),
  user_id    INTEGER NOT NULL REFERENCES users(id),
  name TEXT NOT NULL,                           -- a human label
  data TEXT NOT NULL,                           -- the opaque recorded stream
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

Every row carries `project_id` + `user_id`, and **every query filters on both** —
the same multi-tenant + per-user scoping as the rest of the platform. A player only
ever sees, reads, or deletes their own replays; another user's id in the URL yields
a 404, never someone else's data.

---

## 3. The service (`baas/replays/replay_service.cc`)

Four operations, all scoped, all parameterized SQL:

```cpp
long long create(pid, uid, name, data) {           // returns the new id
    auto r = db->execSqlSync(
        "INSERT INTO replays(project_id,user_id,name,data) VALUES(?,?,?,?)",
        pid, uid, name, data);
    return (long long) r.insertId();               // the server-assigned id
}
std::optional<Record> get(pid, uid, id)  → SELECT ... WHERE id=? AND project_id=? AND user_id=?
std::vector<Meta>     list(pid, uid)     → SELECT id,name,length(data),created_at ... ORDER BY id DESC
bool                  remove(pid, uid, id) → DELETE ... WHERE id=? AND project_id=? AND user_id=?
```

`insertId()` gives us the row's new id to return to the client. `list` returns
metadata only (id, name, size, created_at) — not the payloads — so listing a hundred
replays is cheap; the bytes come only on `get`. Newest-first (`ORDER BY id DESC`) is
what a "recent replays" UI wants.

`valid_name` accepts 1–64 printable-ASCII characters (a label can have spaces,
unlike the stricter slot/key rules elsewhere).

---

## 4. The HTTP edge (`replay_controller.cc`)

REST-shaped, all behind `ApiKeyFilter` + `AuthFilter` (api-key → project, JWT →
user). The user is always the token's, never a body field.

| route | does |
|---|---|
| `POST /v1/replays` `{name,data}` | create → `{id,name,size}` |
| `GET  /v1/replays` | list → `{replays:[{id,name,size,created_at}]}` |
| `GET  /v1/replays/{id}` | fetch → `{id,name,data,created_at}` |
| `DELETE /v1/replays/{id}` | delete → `{deleted:true}` (404 if not yours) |

The `{id}` path param arrives as a string and is parsed with `strtoll`; a bad or
someone-else's id simply misses the `WHERE` and returns 404. Payloads are capped at
**512 KiB** (`kMaxReplayBytes`) — a command stream is tiny, so this is a generous
storage-DoS guard, not a real limit.

---

## 5. The SDK handle (`client.replays()`)

Same non-blocking, callback-per-frame shape as every other handle:

```cpp
client.replays().save("my run", stream, [](gbaas::Result<gbaas::ReplayMeta> r){ /* r->id */ });
client.replays().list([](gbaas::Result<std::vector<gbaas::ReplayMeta>> r){ /* newest first */ });
client.replays().get(id, [](gbaas::Result<gbaas::Replay> r){ /* r->data */ });
client.replays().remove(id, [](gbaas::Result<bool> r){});
```

Crucially, this rides the **existing HTTP transport** — libcurl natively,
`emscripten_fetch` on the web. Unlike realtime (which needed a whole new WebSocket
transport, ch.64), replays needed *zero* new transport work: the web path was
already proven. That is the payoff of the transport seam — a new service is just new
request/response shapes over the same pipe.

---

## 6. Colony: record → cloud → play back

Colony wires the whole loop through three tiny methods:

```cpp
void apply_command(const std::string& cmd) {          // the ONLY place a command acts
    if      (cmd == "spawn")  sim_.spawn_agent(...);
    else if (cmd == "corner") sim_.set_goal(...);
    else if (cmd == "reset")  sim_.reset_default();
}
void issue_command(const std::string& cmd) {          // player action: act + maybe record
    apply_command(cmd);
    if (recording_) rec_buf_ += std::to_string(sim_frame_ - rec_start_) + ":" + cmd + "\n";
}
void update_replay() {                                // during playback, re-issue on schedule
    if (!playing_) return;
    ++play_frame_;
    while (play_idx_ < play_cmds_.size() && play_cmds_[play_idx_].first <= play_frame_)
        apply_command(play_cmds_[play_idx_++].second);
    if (play_idx_ >= play_cmds_.size()) playing_ = false;
}
```

The key structural move: **all three UI buttons (Spawn / Send-to-corner / Reset) go
through `issue_command`, and playback goes through `apply_command`.** So "what a
command does" lives in exactly one function, and recording is a single extra line.
The buttons: **Record replay** → **Stop & Save replay** (calls
`client_.replays().save`), and **Play last replay** (`list` → newest → `get` →
`reset` the sim → feed the commands). A panel line shows `replay: recording /
saved #7 / playing (4 cmds) / done`.

The record→save→list→get→play chain exercises every SDK replay method, native and
web, through the frame-pumped callback model.

---

## 7. Testing

- **`baas_replays`** (integration, real HTTP): auth-required write; create two →
  list is newest-first; get returns the payload; **per-user isolation** (a second
  guest sees none, and gets 404 reading/deleting the first user's replay);
  validation (empty/missing name → 400); size cap (>512 KiB → 413); delete then gone
  (and a second delete → 404).
- **`sdk_client`** (unit, fake transport): the `save/list/get/remove` request
  shapes, data escaping, and response parsing — no server.

Both green; `baas_replays` clean under ASan/UBSan. The web path is the same
`emscripten_fetch` transport already browser-verified for colony's other services,
so it is compile-verified here rather than re-smoke-tested.

---

## 8. Pitfalls

- **Trusting a client-supplied user/owner.** The owner is always the JWT's user;
  the id in the URL only ever matches *your* rows (`WHERE ... AND user_id=?`).
- **Confusing a command replay with a state replay.** We store commands, not state.
  If the sim isn't deterministic, playback re-enacts the *inputs*, not a pixel-exact
  recording — know which guarantee you're giving.
- **One command dispatch, or drift.** If the buttons and the player took different
  code paths from playback, a replay would diverge immediately. Route both through
  `apply_command`.
- **Unbounded payloads.** Cap them (512 KiB) — a replay is small; anything huge is a
  bug or an attack.
- **Frame base for relative timing.** Record `frame - rec_start_`, not the absolute
  frame, so a replay plays back correctly starting from playback-frame 0.

---

## 9. Glossary

- **Command-stream replay** — record the player's commands + timings; reproduce by
  re-running the sim on them. Tiny, needs determinism.
- **State-snapshot replay** — record full state periodically; reproduce by loading
  snapshots. Large, no determinism needed.
- **Determinism** — same inputs → same outputs, bit-for-bit; the property that makes
  command-stream replay exact (and enables lockstep multiplayer).
- **`insertId()`** — the auto-assigned row id libcurl/Drogon's ORM returns after an
  INSERT; here it's the replay's public id.
- **Immutable blob** — a replay is written once and never edited (only created /
  read / deleted), unlike a versioned save slot.

---

## 10. Exercises

1. **Rename / share.** Add `PATCH /v1/replays/{id}` to rename a replay, and a
   `shared` flag + a project-wide "featured replays" list. Which queries drop the
   `user_id` filter, and why is that safe only for explicitly-shared rows?
2. **Playback controls.** Add pause / step / 2× speed to colony playback. *Hint:*
   `update_replay` already advances `play_frame_` — scale or gate the increment.
3. **Snapshot hybrid.** Store a state snapshot every N seconds alongside the command
   stream so playback can *seek* (jump to the middle) without replaying from 0.
4. **True determinism.** Make colony's sim bit-exact (seed all RNG, fix iteration
   order, avoid float non-associativity) and add a test that a replay reproduces the
   final agent positions exactly. This is also the foundation of lockstep netcode.
5. **Anti-cheat via replay.** A replay is a verifiable record. Sketch (don't build) a
   server-side check that re-runs a submitted replay to validate a claimed
   leaderboard score — the hand-buildable core of "anti-cheat".

---

*This closes the hand-buildable roadmap: state (REST), presence (WebSocket), and now
recorded history (replays) — all over the unified SDK, native and web.*
