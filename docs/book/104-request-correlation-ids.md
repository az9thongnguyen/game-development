# Chapter 104 — Correlation IDs: Making Every Response Traceable

> Code: `baas/app_setup.cc` (`new_request_id`, `sanitize_request_id`, the correlation
> pre-routing advice, the pre-sending advice) · `tests/baas_test_util.h` (header capture) ·
> `tests/test_baas_tracing.cc`

The strategy's delivery-and-operations list asks for "structured logs, metrics, traces,
correlation IDs." The access log already existed (Chapter on observability); this slice adds the
one thing that ties a log line to a specific request and lets a caller follow its request across a
proxy: a correlation id on every response.

## The gap: logs you cannot join

The pre-sending advice already logged `METHOD path status duration` for every response. Useful in
aggregate, useless for a single incident: when a player reports "my purchase failed at 3:02," there
is no id connecting their client, the proxy in front of the server, and the exact log line. Two
requests a millisecond apart are indistinguishable in the log. A correlation id fixes that — one
value, minted per request, that appears in the response header the client sees and in the server log
line, so the two can be joined after the fact.

## Adopt-or-mint, in a pre-routing advice

A new pre-routing advice runs the id logic before anything else touches the request:

```cpp
std::string rid = req->getHeader("x-request-id");
rid = rid.empty() ? new_request_id() : sanitize_request_id(rid);
req->attributes()->insert("request_id", rid);
```

If a proxy or client already sent an `X-Request-Id`, the server *adopts* it — that is what makes the
id span systems: the same value the client generated flows through to this server's logs. If not,
the server *mints* one (`new_request_id` — 16 random hex chars from libsodium). The advice is
registered immediately after the start-time stamp and **before** the rate-limit advice, so even a
request rejected with a 429 already carries an id — nothing escapes without one.

Two details matter for safety. First, an inbound id is *client-supplied*, so it is sanitized before
it can reach a log line or a response header: `sanitize_request_id` caps it at 64 characters and
replaces anything outside `[A-Za-z0-9_-]` with `_`. Without that, a value containing a newline could
forge a fake log line, or a value with control characters could corrupt the echoed header. Second,
the pre-sending advice both echoes the id and logs it:

```cpp
const std::string rid = req->attributes()->get<std::string>("request_id");
resp->addHeader("X-Request-Id", rid);
LOG_INFO << "[" << rid << "] " << req->methodString() << " " << req->path() << " " << status ...;
```

The client gets the id back in the response header (so it can quote it in a bug report), and the
same id prefixes the server log line (so support can find it). One value, both sides.

## The proof

Testing a *response header* meant the shared test harness had to capture headers, not just the body
— so `baas_test_util.h` gained a `raw_headers` field and a case-insensitive `header_value` helper
(an additive change; every existing test still compiles). `tests/test_baas_tracing.cc` then boots a
real server and checks the four behaviours:

- A request with no inbound id gets a minted one: present, 16 hex characters.
- A second no-id request gets a **different** id — they are unique per request, or they would be
  useless for correlation.
- An inbound `X-Request-Id: trace-abc_123` is echoed verbatim — the adopt path, for cross-system
  tracing.
- A hostile inbound `id with/special*chars` comes back sanitized to `id_with_special_chars` — the
  injection guard, proven rather than assumed.

The unique-per-request check is the one that keeps the feature honest: an id scheme that ever
repeats would silently join unrelated requests, which is worse than no id at all. Minting from 8
random bytes makes a collision astronomically unlikely, and the test pins that two consecutive mints
differ.
