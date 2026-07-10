# Chapter 52 — Drogon Foundations & the Gateway

> **What this is.** How the backend boots, and the **Gateway** — the filter chain
> every request passes before it reaches a service. You'll learn Drogon's request
> lifecycle, how a *filter* turns an API key into a project (the multi-tenancy
> gate), the one JSON **error envelope** shared by every endpoint, and a subtle
> **linker trap** (self-registering objects vanish from a static archive) that
> dictates how we build the code. Code: `baas/main.cc`, `baas/app_setup.*`,
> `baas/common/`, `baas/gateway/api_key_filter.*`.

---

## 1. Booting Drogon

`main.cc` is small: parse flags, init crypto + config, connect the DB, register
routes, listen, run.

```cpp
drogon::app().registerHandler("/healthz", handler, {drogon::Get});
web::register_routes();
drogon::app().addListener(host, port).run();   // blocking event loop
```

`drogon::app()` is a process-global singleton. `run()` owns the event loop (like the
engine's `platform::run`), so all our code above it is *registration*, done before
`run()`. There is one subtlety we lean on in tests (Chapter 55's harness): you can
call `app().run()` on one thread and `app().quit()` from another to shut it down
cleanly.

## 2. The request lifecycle

Every `/v1/*` request flows through an ordered chain:

```
request ─▶ ApiKeyFilter ─▶ [AuthFilter] ─▶ Controller ─▶ Service ─▶ DbClient ─▶ Response
```

Filters are Drogon's middleware. Each either **rejects** (calls `fcb(response)`) or
**continues** (calls `fccb()`), optionally leaving data on the request for the next
stage. The Gateway is nothing more than this chain: `ApiKeyFilter` resolves the
tenant; `AuthFilter` (Chapter 53) resolves the user; the controller reads what they
left behind.

## 3. Carrying identity: request attributes

Filters resolve identity; controllers read it back. Drogon lets you attach typed
values to a request via `req->attributes()`. Typed means **`insert<T>` and `get<T>`
must agree**, so we centralize the keys (and document their stored type) in one file
to prevent drift:

```cpp
// baas/common/context_keys.h
inline constexpr const char* kProjectId = "project_id";  // long, set by ApiKeyFilter
inline constexpr const char* kUserId    = "user_id";     // long, set by AuthFilter
```

A typo or a `get<int>` where an `insert<long>` happened is a runtime bug; a shared
header makes both sides read from the same source.

## 4. One error shape for everything

The SDK should parse errors the same way regardless of which service failed, so every
failure is `{"error":{"code":"…","message":"…"}}` with a matching HTTP status:

```cpp
// baas/common/errors.cc
drogon::HttpResponsePtr make_error(int status, const std::string& code,
                                   const std::string& message) {
    Json::Value err; err["code"] = code; err["message"] = message;
    Json::Value body; body["error"] = err;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(status));  // 401 → k401…
    return resp;
}
```

The cast is safe: the numeric HTTP codes *are* the enum values.

## 5. The multi-tenancy gate: `ApiKeyFilter`

This is the platform's differentiator in embryo. Every game embeds a project's
**public key** and sends it as `X-Api-Key`; the filter turns it into a `project_id`
and refuses anything it can't resolve. Nothing downstream ever runs without a project.

```cpp
// baas/gateway/api_key_filter.cc  (doFilter)
const std::string key = req->getHeader("x-api-key");        // case-insensitive
if (key.empty()) { fcb(make_error(401, "unauthorized", "missing X-Api-Key")); return; }
const auto rows = db::client()->execSqlSync(
    "SELECT id FROM projects WHERE public_key=?", key);      // parameterized
if (rows.empty()) { fcb(make_error(401, "unauthorized", "invalid X-Api-Key")); return; }
req->attributes()->insert(kProjectId, rows[0]["id"].as<long>());
fccb();                                                      // continue the chain
```

Because *every* query downstream also filters by this `project_id` (Chapter 54), one
tenant can never touch another's data — a property we prove with a test in Chapter 55.

> **A note on `execSqlSync`.** It blocks the event-loop thread until the query
> returns. For SQLite and demo-scale traffic that is microseconds and fine; the code
> flags where to switch to Drogon's async `execSqlAsync` if throughput ever demands
> it. Naming the ceiling is part of the design, not a defect.

## 6. Registering routes, and referencing filters by name

Plain lambda routes live in `app_setup.cc` so the server **and** the tests set up an
identical app:

```cpp
// authenticated liveness — proves the filter runs and attaches the project
drogon::app().registerHandler("/v1/ping", handler,
    {drogon::Get, std::string("web::ApiKeyFilter")});
```

Filters are referenced by their **fully-qualified class name** — `"web::ApiKeyFilter"`,
not `"ApiKeyFilter"`. Drogon registers a filter under its demangled type name
(namespace included); get this wrong and Drogon logs *"middleware … not found"* and
runs the route *without* the filter — an open door that a test must catch (ours does).

## 7. The linker trap that shapes the build

Drogon filters and controllers **self-register** via a static initializer that runs
when their translation unit is loaded. Put those objects in a **static library** and
the linker will *drop* any member with no referenced symbol — so the initializer
never runs and the filter silently doesn't exist. We hit exactly this: the first
build compiled `ApiKeyFilter` into `libbaas_core.a`, nothing referenced it, and the
`/v1/ping` test got `200` without a key.

The fix is to make `baas_core` an **OBJECT library**:

```cmake
add_library(baas_core OBJECT db/db.cc common/errors.cc gateway/api_key_filter.cc …)
```

An OBJECT library compiles every `.o` straight into each target that links it — no
archive-member selection — so every registration runs. This is why the whole backend
(minus `main.cc`) is one OBJECT library that both `baas` and every test link. (The
platform-specific alternative is `-Wl,-force_load` / `--whole-archive`; OBJECT is
cleaner and portable.)

## 8. Checkpoints

- Trace a `GET /v1/leaderboards/colony_high/top` request through the chain — which
  filter runs, what does it leave on the request, what returns it a 401?
- Why must a filter be referenced as `"web::ApiKeyFilter"` and not `"ApiKeyFilter"`?
- Explain the OBJECT-vs-STATIC library choice to someone who's never linked C++.
