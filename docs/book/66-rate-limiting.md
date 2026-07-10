# Chapter 66 — Rate limiting: abuse protection at the gateway

> **Where we are.** The platform is feature-complete for what a game needs: auth,
> the six L1 services, a dashboard, realtime, and replays. This chapter is about
> making it *safe to expose* — a production concern rather than a feature. A public
> API with no rate limit is one buggy client (or one attacker) away from a flooded
> database. We add a **token-bucket rate limiter** at the gateway, so an abusive
> caller is rejected cheaply, before any per-request work.

---

## 1. Why rate limiting, and why at the gateway

Every endpoint we built does real work per request: a SQL query, an argon2 hash
(login), a JSON parse. That is fine at human speed. But nothing stops a client from
sending 10,000 requests/second — a runaway retry loop, a scraper, or a
denial-of-service attempt. Without a limit, the server happily tries to serve all of
them, the database saturates, and *every* user suffers.

The fix is to cap how fast any one caller may hit the API, and to enforce it **as
early as possible** — before routing, before the DB, before the hash. That is the
gateway's job (the same gateway that already resolves api-keys and JWTs). Reject the
excess with **HTTP 429 Too Many Requests** and the expensive machinery never runs.

```
   request ──▶ [ rate limit ] ──▶ [ routing ] ──▶ [ ApiKeyFilter ] ──▶ [ handler ] ──▶ DB
                    │ over limit?
                    └──▶ 429, stop here (no routing, no DB, no hash)
```

---

## 2. The algorithm: token bucket

There are two classic rate-limit algorithms:

- **Fixed window** — "≤ N requests per calendar minute". Simple, but it allows a
  *double burst* at a window boundary (N at 0:59, N at 1:00 = 2N in two seconds).
- **Token bucket** — a bucket holds up to `capacity` tokens and refills at
  `refill_per_sec`. Each request spends one token; if the bucket is empty, reject.
  It permits a *burst* up to `capacity` (good — real clients are bursty) but caps the
  *sustained* rate at the refill rate. No boundary double-burst.

We use a token bucket. The mental model:

```
capacity = 5, refill = 1/sec

t=0.0  ●●●●●   5 tokens        request → spend 1 → ●●●●   (4 left)
...    (5 quick requests)      → ●●●●●  drained to empty; 6th → 429
t=2.0  ●●      2 refilled (2s × 1/s)   → 2 more allowed, then 429
t=100  ●●●●●   capped at 5 (NOT 5+100) — idle doesn't bank infinite tokens
```

That "capped at capacity" line is the important subtlety: a bucket that has been
idle for an hour does **not** accumulate 3600 tokens. It tops out at `capacity`, so
a long-idle client still can't unleash an unbounded burst.

---

## 3. A pure, testable limiter

The whole algorithm is a few lines — but time makes it awkward to test if we read
the clock inside. So the limiter is **pure with respect to time**: the caller passes
`now`. That makes it deterministically testable (no `sleep`), and the gateway simply
supplies a real monotonic clock.

```cpp
bool RateLimiter::allow(const std::string& key, double now) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = buckets_.find(key);
    if (it == buckets_.end()) {                       // first sight → a full bucket
        buckets_[key] = Bucket{capacity_ - 1.0, now}; // (minus the token we spend now)
        return capacity_ >= 1.0;
    }
    Bucket& b = it->second;
    double elapsed = now - b.last;
    if (elapsed > 0) { b.tokens = std::min(capacity_, b.tokens + elapsed * refill_); b.last = now; }
    if (b.tokens >= 1.0) { b.tokens -= 1.0; return true; }
    return false;
}
```

Key points:

- **Lazy refill.** We don't run a timer adding tokens; we compute how many *would*
  have accrued since `last` when a request arrives. No background thread, no wasted
  work on idle keys.
- **`std::min(capacity_, …)`** enforces the ceiling.
- **One mutex.** Same reasoning as the realtime hub (ch.63): the critical section is
  a map lookup + a little arithmetic; contention is negligible at single-node scale.
  `// ponytail: shard + evict idle buckets if the key space ever gets huge.`

Because it's pure, `test_rate_limiter` verifies every property on an *injected*
timeline — burst, refill, ceiling, per-key independence, reset, and the disabled
(`capacity 0`) case — in microseconds, with no flakiness.

---

## 4. Wiring it in: a pre-routing advice

Drogon lets you register advice that runs **before routing**, for every request.
That is exactly where a global rate limit belongs — no per-controller edits, and it
fires before the router even decides which handler to call:

```cpp
if (config().rate_capacity > 0) {                     // only when enabled
    static RateLimiter limiter(config().rate_capacity, config().rate_refill_per_sec);
    static const auto  t0 = std::chrono::steady_clock::now();
    app().registerPreRoutingAdvice(
        [](const HttpRequestPtr& req, AdviceCallback&& stop, AdviceChainCallback&& pass) {
            if (req->path().rfind("/v1/", 0) != 0) { pass(); return; }   // API routes only
            std::string key = req->getHeader("x-api-key");
            if (key.empty()) key = "ip:" + req->getPeerAddr().toIp();
            double now = std::chrono::duration<double>(steady_clock::now() - t0).count();
            if (limiter.allow(key, now)) pass();
            else stop(make_error(429, "rate_limited", "too many requests"));
        });
}
```

Three deliberate choices:

- **`/v1/*` only.** Static assets (the dashboard, the WASM build, `demo.wasm`,
  `demo.data`) are served from the same app. A web game loads *many* asset files on
  startup; throttling those would break the demo. So the limiter applies only to the
  API namespace and lets everything else — including `/healthz` — pass untouched.
- **Key by api-key, fall back to IP.** An authenticated caller is limited by *who
  they are* (their project's api-key); a keyless request is limited by *where it's
  from* (`ip:1.2.3.4`), so a floods-without-a-key still get throttled. We key on the
  raw header *before* the api-key is validated, so even invalid-key floods are cheap
  to reject.
- **Enabled only when `capacity > 0`.** The advice isn't even registered otherwise,
  so tests and dev runs that don't set a limit pay nothing. The real server
  (`main.cc`) turns it on by default (burst 120, 60/sec per caller) and lets env
  vars (`BAAS_RATE_CAPACITY`, `BAAS_RATE_REFILL`; `=0` disables) tune or kill it.

---

## 5. Testing

- **`rate_limiter`** (unit, no server): the pure algorithm on an injected clock —
  burst, refill, ceiling, per-key independence, reset, disabled.
- **`baas_ratelimit`** (integration): with a low no-refill limit (capacity 3), a
  caller's 4th `/v1/ping` in a burst returns **429** with `{"error":{"code":
  "rate_limited"}}`; `/healthz` is hammered 10× and never throttled; and a *different*
  api-key sails past the limiter to the api-key check (401 for an invalid key, not
  429) — proving buckets are per-caller.

Both green; the limiter is exercised by the pure unit test under ASan/UBSan too.

---

## 6. What this is and isn't

This is **coarse, single-node, in-process** rate limiting — the right first layer,
and enough to stop a runaway client or a naive flood from taking the service down.
It is *not*:

- **Distributed.** Across N server nodes, each has its own buckets, so the effective
  limit is N×. A real multi-node deploy shares buckets in Redis (or fronts the fleet
  with an API gateway / CDN that rate-limits). `// ponytail:` the `RateLimiter`
  interface is the seam to swap in a shared backend.
- **DDoS protection.** A large distributed attack is absorbed upstream (CDN,
  L3/L4 scrubbing), not in application code. This layer protects against
  *application-level* abuse, which is the part we own.

Naming the ceiling is the point: it's honest about where this guard ends.

---

## 7. Pitfalls

- **Throttling static assets.** Scope the limiter to the API, or a web build that
  pulls 30 files on load will 429 itself. (`/v1/*` gate.)
- **Reading the wall clock, not a monotonic one.** `system_clock` can jump
  backwards (NTP), corrupting elapsed-time math. Use `steady_clock`.
- **Keying by something forgeable for auth'd traffic.** We key auth'd callers by
  their api-key (a credential), not a spoofable header; keyless traffic by IP.
- **Refilling with a timer thread.** Unnecessary — lazy refill on access is simpler
  and does zero work for idle keys.
- **Forgetting to exempt liveness.** If `/healthz` is throttled, your orchestrator
  may mark a busy-but-healthy node dead. Keep probes out of the limiter.

---

## 8. Glossary

- **Token bucket** — a rate-limit algorithm: a refilling bucket of tokens, one spent
  per request; permits bounded bursts, caps sustained rate.
- **Capacity / burst** — the max tokens a bucket holds (the largest instantaneous
  burst allowed).
- **Refill rate** — tokens added per second (the sustained request rate allowed).
- **Pre-routing advice** — a Drogon hook that runs for every request before routing;
  the earliest, cheapest place to reject abuse.
- **429 Too Many Requests** — the HTTP status for "you're being rate limited".
- **Lazy refill** — computing accrued tokens on access instead of via a timer.

---

## 9. Exercises

1. **`Retry-After`.** Add a `Retry-After` header (seconds until one token refills) to
   the 429 response so well-behaved clients back off precisely.
2. **Per-route cost.** Make expensive routes (login → argon2) spend more than one
   token. *Hint:* an `allow(key, now, cost)` overload.
3. **Idle eviction.** Buckets untouched for an hour leak memory. Add a periodic sweep
   (or evict on access when the map grows past a threshold). What's the trade-off vs.
   just capping the map size?
4. **Distributed limiter.** Design (don't build) a Redis-backed `RateLimiter` with
   the same interface. Which operation must be atomic, and why is a naive
   get-modify-set wrong under concurrency?
5. **Tiered limits.** Give each project a plan (free/pro) with its own capacity, read
   from the `projects` row. Where does the per-project config get loaded so the hot
   path stays fast?

---

*This closes the platform's production-hardening pass. State (REST), presence
(WebSocket), recorded history (replays), and now a gateway that protects them all.*
