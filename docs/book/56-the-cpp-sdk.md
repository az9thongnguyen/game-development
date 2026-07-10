# Chapter 56 — The Unified C++ SDK

> **What this is.** The client half of the platform: a small C++ library
> (`gbaas_sdk`) a game links to talk to the backend. Its two defining ideas are
> the ones that made the engine web-portable — a **non-blocking** API pumped from
> the game loop, and a **transport seam** with a native (libcurl) and a web
> (emscripten_fetch) backend. You'll also see why it carries its own tiny JSON.
> Code: `sdk/cpp/include/gbaas/*`, `sdk/cpp/src/*`.

---

## 1. The shape of a call

```cpp
gbaas::Client c({.base_url = "http://127.0.0.1:8080", .api_key = "pk_demo_colony"});
c.auth().guest([&](gbaas::Result<gbaas::Session> r) {
    if (r) online = true;                       // r->user_id, r->display_name, r->is_guest
});
c.leaderboard("colony_high").submit(4200, [&](gbaas::Result<gbaas::Rank> r) { ... });
// once per frame:
c.update();
```

Every service reads the same: `client.<service>().<action>(args, callback)`. Every
result is a `Result<T>` — a value on success, or an `Error{code, message, status}`
on failure — so calling code has exactly one shape to handle. The api key rides on
every request; after a successful auth the access token is attached automatically.

## 2. Non-blocking, pumped from the tick

The engine forbids a blocking loop above the platform layer (so it can run under the
browser's event loop). The SDK obeys the same rule: a call **returns immediately**
and its callback fires later, when you pump `client.update()` — once per frame, from
the game's `update`. Nothing ever blocks the game waiting on the network.

`update()` just drives the transport's `poll()`. This is why the colony scene calls
`client_.update()` at the top of its `update()`, before anything else.

## 3. The transport seam

This is the same trick as `platform.hpp` (`backend_sdl.cpp` vs `backend_web.cpp`).
The SDK logic talks only to an interface:

```cpp
struct ITransport {
    virtual void send(method, url, headers, body, HttpDone done) = 0;  // async
    virtual void poll() = 0;                                           // pump completions
};
```

Two implementations, chosen by CMake:

- **`HttpTransportCurl`** (native) uses libcurl's **multi** interface: `send()` queues
  an easy handle and returns; `poll()` runs one non-blocking `curl_multi_perform`
  pass and fires the callback of any transfer that finished. Per-request state is
  heap-owned (stable addresses for libcurl) and freed *before* the callback runs, so
  a callback may safely enqueue the next request.
- **`HttpTransportEmscripten`** (web) uses `emscripten_fetch`, which is already async
  and driven by the browser — so `send()` just starts a fetch and `poll()` is a
  no-op; the fetch's success/error handlers deliver the response. Request data must
  outlive the async call, so body + header strings + callback live in a heap context
  freed in the handler.

`Client` doesn't know or care which one it has. The tests use a third: a **fake**
transport that records the request and returns a canned response — that is how
`test_sdk_client` covers assembly, token handling, parsing, and the error path with
no server at all.

## 4. Its own JSON

The SDK is linked into a *game*, native and WASM. It must not drag in Drogon's
jsoncpp (a server dependency), so it carries a **minimal JSON** — parse + string
escape — in `json.hpp`. It's deliberately small (a recursive-descent parser and an
escaper, handling UTF-8 `\u` escapes and surrogate pairs), not a general library.
This mirrors the whole project's instinct: pull a big dependency only where it earns
its weight (Drogon, on the server), and hand-write the small, self-contained piece
where a dependency would leak across a boundary (JSON, on the client).

## 5. Wire contract, not code, is the SDK

The SDK speaks plain REST/JSON. That matters beyond C++: the eventual Unity (C#) and
Unreal SDKs re-implement the *same wire contract*, not the SDK's internals. Keeping
the protocol simple and debuggable with `curl` is what makes three SDKs feasible
without three rewrites.

## 6. Building it

`gbaas_sdk` is a normal static library (`sdk/cpp/CMakeLists.txt`): the shared
`client.cc` plus one transport `.cc` chosen by platform, libcurl on native and
`-sFETCH` on web. It links **no** server code, and the engine core links **no** SDK
— the game is the only thing that links both.

## 7. Checkpoints

- Where does a callback actually run — during the call, or during `update()`? Why
  does that design keep the game loop responsive?
- Name the two real transports and the fake one, and what each is for.
- Why does the SDK carry its own JSON instead of reusing the server's jsoncpp?
