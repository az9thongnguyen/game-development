# Chapter 55 — The Leaderboard Service

> **What this is.** The first real feature on the spine: a global leaderboard with
> ranking, "keep your best" upsert, authenticated writes, and two guarantees that
> matter — you can't **spoof** another player's score, and one project can't **see**
> another's. You'll also meet the integration-test harness that boots the real
> server and drives it with `curl`. Code: `baas/leaderboard/{lb_service,lb_controller}.*`,
> `tests/test_baas_leaderboard.cc`, `tests/baas_test_util.h`.

---

## 1. The three operations

```
GET  /v1/leaderboards/{key}/top?limit=N   api-key           → { entries: [...] }
POST /v1/leaderboards/{key}/scores {value} api-key + JWT     → { rank, value, updated }
GET  /v1/leaderboards/{key}/me            api-key + JWT      → { rank, value }
```

A board is resolved once from `(project_id, key)` — `404` if the project has no such
board — and the resolved `Board{id, desc}` is passed to each operation, keeping the
queries focused. `desc` (from the board's `sort`) decides whether higher or lower is
better.

## 2. Ranking

Rank is defined simply and computed in SQL: **your rank is the number of strictly
better scores, plus one.**

```cpp
// baas/leaderboard/lb_service.cc
const char* better_than(bool desc) { return desc ? ">" : "<"; }   // fixed token, not user input

int rank_for_value(const Board& b, long value) {
    auto rows = db::client()->execSqlSync(
        std::string("SELECT count(*) AS c FROM scores WHERE leaderboard_id=? AND value ")
            + better_than(b.desc) + " ?",
        b.id, value);
    return (int)rows[0]["c"].as<long>() + 1;
}
```

Note the only concatenation is the `>`/`<` operator, chosen by a `bool` we control;
`value` is still bound as a parameter. `top` orders by value (`ASC`/`DESC` keyword,
again code-chosen) with `updated_at` as a tie-break so the earliest to reach a value
ranks higher, and joins `users` for display names.

## 3. "Keep your best"

Submitting doesn't blindly overwrite — it keeps the better of old and new per the
board's sort. The logic is a read-then-branch (SQLite serializes writers, so the
race is a non-issue here; a comment marks it):

```cpp
if (existing.empty()) {                       // first score
    INSERT …; updated = true;
} else {
    bool better = board.desc ? (value > old) : (value < old);
    if (better) { UPDATE … SET value=? …; updated = true; }
    else        { final_value = old; }        // keep the existing, report updated=false
}
return { rank_for_value(board, final_value), final_value, updated };
```

The response tells the client both the new standing and whether anything changed
(`updated`), which the game uses to decide whether to celebrate a new personal best.

## 4. Two guarantees, enforced by the controller + the filters

**Anti-spoof.** The score's owner is taken from the **verified JWT**, never the
request body:

```cpp
const long uid = req->attributes()->get<long>(kUserId);   // from AuthFilter, NOT the body
```

A malicious client can send `{"value":999,"user_id":<someone else>}` all it likes; the
`user_id` field is simply ignored. The write is always attributed to the token's user.
Our test submits exactly that payload and asserts the *attacker's* score changed and
the *victim's* did not.

**Tenant isolation.** Every query is scoped to a board that belongs to the request's
project (Chapter 54). The test creates a second project B with its own `colony_high`
board, has B submit a huge score, and asserts A's `top` never contains B's user — and
vice-versa.

**Input bounds.** `value` is range-checked (`|value| ≤ 1e12`) — a first, cheap line
against absurd/overflow submissions. Real anti-cheat (server-authoritative scoring,
rate limits, anomaly detection) is a later layer (L4); this is the honest floor, and
the code says so.

## 5. Testing a real server without a browser

`tests/baas_test_util.h` is the shared harness the integration tests use. The pattern
(also in Chapter 52's `test_baas_auth`): boot the **actual Drogon app** on an
OS-assigned free port against a **temp SQLite DB**, then drive it with a synchronous
**libcurl** client.

```cpp
drogon::app().addListener("127.0.0.1", port);
std::thread tester([&]{
    /* wait for /healthz, then run assertions with baastest::http(...) */
    drogon::app().quit();                 // stop the server from the worker thread
});
drogon::app().run();                      // app on the main thread; blocks until quit()
tester.join();
```

Running the app on the main thread (clean signal handling) and the requests on a
worker that calls `quit()` when done is the trick that makes a blocking event loop
unit-testable. `baastest::parse()` turns responses into `Json::Value` for precise
assertions (`entries[0]["user_id"]`, `updated == false`, matching error codes, …).

## 6. What the leaderboard test actually proves

`test_baas_leaderboard.cc`, end to end: writes require a JWT (401 without) · submit
ranks correctly · higher replaces, lower is ignored (`updated` flips accordingly) ·
`me` reports rank+value · **anti-spoof** · **cross-tenant isolation** · absurd value →
400 · unknown board → 404. Green here means the backend half of the walking skeleton
is genuinely done — not "compiles," but "behaves."

## 7. Checkpoints

- Define rank in one sentence, then explain the `>`/`<` trick and why it's not SQL
  injection.
- A player resubmits a *worse* score. What does the response say, and what's stored?
- Sketch the two-project setup the isolation test uses. What single missing SQL clause
  would make it fail — and leak data in production?
