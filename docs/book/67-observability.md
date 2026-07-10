# Chapter 67 — Observability: metrics & structured access logs

> **Where we are.** Ch.66 added rate limiting — the platform can now defend itself.
> This chapter adds the other half of running a service in production: being able to
> *see* what it's doing. Without observability, "is it healthy?" and "why is it
> slow?" are guesses. We add two of the three classic signals — **metrics** (counts)
> and **structured logs** — with one small, well-placed hook.

---

## 1. The three signals, and which two we build

Production observability is usually described as three signals:

- **Metrics** — cheap numeric aggregates over time: request count, error rate,
  latency percentiles. Answer "*is the system healthy, and is it getting worse?*"
- **Logs** — a record of individual events. Answer "*what exactly happened to this
  request?*"
- **Traces** — the path of one request across many services. Answer "*where did the
  time go across the system?*"

We build **metrics** and **structured logs**. Distributed tracing needs a trace-id
propagated across service hops and a collector (Jaeger/Zipkin) — real infrastructure,
out of scope — but we note where a trace-id would attach (an exercise).

---

## 2. One hook to see every response

Both signals need the same thing: a callback that fires for **every response**, with
access to the request and the final status. Drogon provides exactly that —
`registerPreSendingAdvice(fn(req, resp))` runs just before any response is sent,
*including* 404s and the pre-routing 429s from the rate limiter. That "including
everything" is the point: a hook that only saw handler responses would miss the
errors you most want to count.

```cpp
app().registerPreSendingAdvice([elapsed](const HttpRequestPtr& req, const HttpResponsePtr& resp) {
    const int status = (int) resp->getStatusCode();
    Metrics::instance().record(req->path(), status);                 // metric
    const double ms = (elapsed() - req->attributes()->get<double>("t_start")) * 1000.0;
    LOG_INFO << req->methodString() << " " << req->path() << " " << status << " " << ms << "ms";  // log
});
```

### Timing without a stopwatch in every handler

Latency is the single most useful signal, but a handler shouldn't have to time
itself. So a **separate pre-routing advice, registered first**, stamps a start time
into the request's attributes; the pre-sending advice reads it back and subtracts:

```cpp
app().registerPreRoutingAdvice([elapsed](const auto& req, auto&&, auto&& pass) {
    req->attributes()->insert("t_start", elapsed());   // one monotonic origin, captured
    pass();
});
```

Registration order matters: the stamp advice is registered **before** the rate
limiter, so even a request that gets 429'd at the gateway still carries a start
stamp and shows up — correctly timed — in the access log. Both advices measure the
same monotonic origin (`steady_clock`, captured by value), so the subtraction is
sound even if the wall clock jumps.

---

## 3. Metrics: counts that stay cheap

The store (`baas/observability/metrics.cc`) is deliberately tiny: a total, a tally by
**status class**, and a tally by **route**. Three counters, one mutex.

```cpp
void Metrics::record(const std::string& path, int status) {
    const char* cls = status/100 == 2 ? "2xx" : status/100 == 4 ? "4xx" : ...;
    lock;
    ++total_;
    ++by_status_[cls];
    ++by_path_[normalize_path(path)];
}
```

### Cardinality: the trap in per-route metrics

The subtle part is `normalize_path`. If we keyed metrics by the *raw* path, then
`/v1/replays/1`, `/v1/replays/2`, … `/v1/replays/999999` would each be a distinct
key — an unbounded map that grows with traffic and is useless to read. This is the
**cardinality explosion** every metrics system warns about. We collapse each path to
its first two segments — its *route group*:

```
/v1/replays/42                      → /v1/replays
/v1/leaderboards/colony_high/top    → /v1/leaderboards
/healthz                            → /healthz
```

Now the key space is bounded by the number of routes, not the number of ids. (Real
systems do the same with route templates like `/v1/replays/{id}`; two-segment
collapse is the lazy, honest version — `// ponytail: good enough; swap in true route
templates if you need /top vs /scores split out.`)

### Reading it: `GET /metrics`

An **admin-gated** endpoint (`X-Admin-Secret`, the same platform-admin gate as
project provisioning) returns the snapshot as JSON:

```json
{ "total": 4,
  "by_status": { "2xx": 2, "4xx": 2 },
  "by_path":   { "/healthz": 1, "/v1/ping": 1, "/v1/auth": 1, "/metrics": 1 } }
```

Why gated? On a public API, request counts and error rates are operational data you
don't hand to anyone. (A Prometheus deployment would instead expose `/metrics` on a
private port or behind network policy — same intent, different mechanism.)

---

## 4. Structured logs

The access line — `GET /v1/replays 200 1.83ms` — is emitted per request via trantor's
`LOG_INFO`. It is "structured" in the sense that it always has the same fields in the
same order (method, path, status, duration), so `grep`/`awk`/a log pipeline can parse
it. In tests the log level is `kError`, so these lines stay quiet; the real server
runs at info and prints them.

> A fully structured logger would emit JSON (`{"method":"GET","status":200,...}`) for
> machine ingestion. We keep a readable line here; making it JSON is a small change
> and a good exercise — the *hook* is the reusable part.

---

## 5. Testing

Because the counter logic is pure, it's unit-tested with no server:

- **`metrics`** (unit): `normalize_path` on representative paths (per-id collapse,
  root, empty); `record`→`snapshot` tallies by status class and route; `reset`.
- **`baas_metrics`** (integration): reset after startup probes, then a *known* set of
  four requests (a 200, a 401, a 200, a 401), then scrape `/metrics` and assert the
  exact totals — plus that the endpoint is refused without the admin secret and that
  the scrape's own request isn't yet in its own snapshot.

Both green; the pure logic is clean under ASan/UBSan.

---

## 6. What this is, and what it isn't

This is **single-process, in-memory, counter-level** observability — enough to answer
"how much traffic, what error rate, which routes, how fast" for one node. It is not:

- **Persistent.** Counters reset on restart. A real deploy scrapes `/metrics`
  periodically into a time-series DB (Prometheus) so history survives restarts.
- **Distributed.** Each node counts itself; the scraper sums across the fleet. And it
  has **no latency histogram** — only a per-request log line — so you can't compute a
  p99 from `/metrics` alone (an exercise).
- **Tracing.** No cross-service request correlation. The seam: put an `X-Request-Id`
  on ingress, log it, and propagate it downstream.

Naming the ceiling is the honest part: this is the first, load-bearing layer, and the
interfaces (`Metrics`, the advices) are where the heavier machinery would plug in.

---

## 7. Pitfalls

- **Unbounded label cardinality.** Never key metrics by raw ids/paths — normalize to
  route groups, or the store grows forever.
- **A hook that misses errors.** Count in *pre-sending* (fires for 404/429/500 too),
  not in a per-controller hook that only sees handler successes.
- **Timing with the wall clock.** `system_clock` can jump; use `steady_clock` for
  durations.
- **Leaking ops data.** Gate `/metrics` (and never log secrets/tokens/passwords in
  the access line — we log only method/path/status/duration).
- **Advice order.** Register the timing stamp before the rate limiter so rejected
  requests are still timed and logged.

---

## 8. Glossary

- **Observability** — the ability to understand a system's internal state from its
  outputs (metrics, logs, traces).
- **Metric** — a numeric aggregate over time (counter, gauge, histogram).
- **Cardinality** — the number of distinct label/key combinations; high cardinality
  (e.g. per-id) is the classic metrics footgun.
- **Status class** — the HTTP status grouped by hundreds (2xx/4xx/5xx).
- **Pre-sending advice** — a Drogon hook that runs for every response before it's
  sent; the one place to observe *all* responses.
- **Structured log** — a log line with consistent, parseable fields.
- **Trace** — the correlated record of one request across services (not built here).

---

## 9. Exercises

1. **Latency histogram.** Add per-route latency buckets (e.g. ≤10ms, ≤50ms, ≤200ms,
   >200ms) to `Metrics` so `/metrics` can approximate a p95. Why buckets and not a
   list of every duration?
2. **Prometheus format.** Add `GET /metrics?format=prom` that emits the Prometheus
   text exposition format (`http_requests_total{status="2xx"} 2`). What changes about
   how a scraper consumes it?
3. **Request ids + tracing seam.** Assign each request an `X-Request-Id` (honor an
   inbound one if present), log it, and echo it on the response. This is the hook a
   distributed tracer attaches to.
4. **JSON logs.** Switch the access line to one-line JSON. What breaks for a human
   reading the terminal, and what gets easier for a log pipeline?
5. **Idle-safe counters.** The maps only grow. For a very long-lived process with
   many transient routes, when (if ever) would you evict, and how do you avoid losing
   a count mid-scrape?

---

*This completes the platform's production-hardening: it defends itself (rate
limiting) and it can be watched (metrics + logs). The service is now not just
feature-complete but operable.*
