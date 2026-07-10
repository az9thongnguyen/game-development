# Chapter 64 — The realtime SDK channel & the live demo

> **Where we are.** Ch.63 built the server: a WebSocket endpoint, a mutex-guarded
> hub, lobby + matchmaking, tenant isolation. This chapter builds the *client* side
> so a C++ game can use realtime the way it already uses every other service —
> `client.realtime().join("lobby")` — and shows the whole thing working live in a
> browser via the dashboard's Realtime console.

---

## 1. The design problem: a persistent channel in a frame-driven SDK

The REST half of the SDK (ch.56) is **request-shaped**: you call, a callback fires
later when `Client::update()` pumps the transport, and the request is done. Realtime
is different — it is a *standing* connection that:

- opens once and stays open for many frames;
- must never block the game loop (same rule as the engine: no blocking loop);
- can deliver events at *any* time, not just in reply to something you sent.

So the realtime handle can't be a throwaway value type like `client.leaderboard(k)`.
It is **stateful and long-lived**, owned by the `Client`:

```cpp
auto& rt = client.realtime();   // created once, lives with the Client
rt.connect();                    // after a successful auth (needs the token)
rt.join("lobby-1");              // or rt.queue() for matchmaking
// each frame:
client.update();                 // pumps HTTP *and* the realtime channel
gbaas::RtEvent ev;
while (rt.poll(ev)) {            // drain everything that arrived this frame
    if (ev.ev == "msg") show_chat(ev.name, ev.data);
    else if (ev.ev == "peer_joined") add_player(ev.from, ev.name);
    // ...
}
```

The mental model: **you push ops immediately; you pull events each frame.** That
matches the game tick perfectly — no threads, no callbacks-within-callbacks, no
blocking.

---

## 2. The transport seam (again)

Just as HTTP has `ITransport` with a libcurl backend (native) and an
`emscripten_fetch` backend (web), realtime has **`IWsTransport`**:

```cpp
struct IWsTransport {
    virtual bool open(const std::string& url) = 0;   // ws://host/path?query
    virtual void close() = 0;
    virtual bool connected() const = 0;
    virtual bool send_text(const std::string& text) = 0;
    virtual bool poll(std::vector<std::string>& out) = 0;   // false = dropped
};
std::unique_ptr<IWsTransport> make_default_ws_transport();   // platform picks one
```

`poll()` is the non-blocking pump: it appends any received text frames to `out` and
returns `false` if the socket dropped. Everything above this interface — building op
frames, parsing event frames — is platform-agnostic and lives in `Realtime`. The
same seam that let us swap SDL backends and HTTP backends now swaps WebSocket
backends, and it is *also* the seam our unit test exploits with a fake.

```
      gbaas::Realtime  (op framing + event parsing — one implementation)
             │  talks only to
             ▼
      gbaas::IWsTransport
       ├── WsTransportCurl        (native: libcurl ws://)          [ws_transport_curl.cc]
       ├── WsTransportEmscripten  (web: browser WebSocket)         [ws_transport_emscripten.cc]
       └── FakeWs                 (tests: records sent, replays canned)  [test_sdk_realtime.cc]
```

---

## 3. Native transport: libcurl `ws://`

libcurl gained WebSocket support in 7.86. The idiom is unusual — you connect in
"CONNECT_ONLY" mode, then push/pull frames yourself:

```cpp
bool open(const std::string& url) {
    curl_ = curl_easy_init();
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L);   // 2 = WebSocket
    if (curl_easy_perform(curl_) != CURLE_OK) return false;  // does the handshake
    // make the socket non-blocking so poll() never stalls:
    curl_socket_t sock; curl_easy_getinfo(curl_, CURLINFO_ACTIVESOCKET, &sock);
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
    connected_ = true; return true;
}
bool send_text(const std::string& t) {
    size_t sent; return curl_ws_send(curl_, t.data(), t.size(), &sent, 0, CURLWS_TEXT) == CURLE_OK;
}
bool poll(std::vector<std::string>& out) {
    for (;;) {
        char buf[4096]; size_t got; const curl_ws_frame* meta;
        CURLcode r = curl_ws_recv(curl_, buf, sizeof buf, &got, &meta);
        if (r == CURLE_AGAIN) return true;                 // nothing more now
        if (r != CURLE_OK)   { connected_ = false; return false; }
        if (meta->flags & CURLWS_CLOSE) { connected_ = false; return false; }
        if (meta->flags & CURLWS_TEXT) {
            pending_.append(buf, got);
            if (meta->bytesleft == 0) { out.push_back(std::move(pending_)); pending_.clear(); }
        }
    }
}
```

Two subtleties:

- **Non-blocking read.** `curl_ws_recv` blocks on a blocking socket. We flip the
  socket to `O_NONBLOCK` after the handshake so `curl_ws_recv` returns `CURLE_AGAIN`
  when there's nothing to read — and `poll()` returns instead of stalling the frame.
- **Fragmentation.** A single logical message can arrive in several fragments
  (`meta->bytesleft > 0` until the last). We accumulate into `pending_` and only
  emit when `bytesleft == 0`.

> `// ponytail:` `open()` does a one-time **blocking** handshake — negligible on
> localhost, and simpler than an async connect state machine. If a game ever needs
> to connect mid-frame without a hitch, that's the upgrade path.

### The keg-only-curl wrinkle (a real build lesson)

macOS ships a system libcurl that is **too old** for `ws://`. Homebrew's curl has
it, but Homebrew keeps curl *keg-only* (not symlinked onto the default include
path), so a naïve `find_package(CURL)` finds the system one. The SDK's CMake
therefore probes for a WebSocket-capable curl explicitly:

```cmake
find_path(GBAAS_CURL_WS_INC curl/websockets.h HINTS /opt/homebrew/opt/curl/include ...)
find_library(GBAAS_CURL_WS_LIB NAMES curl     HINTS /opt/homebrew/opt/curl/lib ...)
if(GBAAS_CURL_WS_INC AND GBAAS_CURL_WS_LIB)
    target_link_libraries(gbaas_sdk PUBLIC ${GBAAS_CURL_WS_LIB})
    target_compile_definitions(gbaas_sdk PRIVATE GBAAS_HAS_WS_CURL=1)
else()   # fall back: REST still works; native realtime is an inert stub
    find_package(CURL REQUIRED)
endif()
```

When no WS-capable curl exists, `ws_transport_curl.cc` compiles to a **stub** whose
`open()` returns `false`. The REST half is unaffected; only realtime is inert, and
the SDK still builds everywhere. This is the honest way to ship a feature that
depends on an environment capability: *detect it, use it when present, degrade
cleanly when absent* — never break the whole build over an optional dependency.

---

## 4. Web transport: the browser's WebSocket

On the web, WebSocket is *native* to the browser — no library to fight. Emscripten
exposes it through `emscripten/websocket.h` (callbacks: `onopen`, `onmessage`,
`onclose`, `onerror`). Opening is asynchronous, so `connected()` flips true from the
`onopen` callback; received text lands in an inbox that `poll()` drains:

```cpp
bool open(const std::string& url) {
    EmscriptenWebSocketCreateAttributes a; emscripten_websocket_init_create_attributes(&a);
    a.url = url.c_str(); sock_ = emscripten_websocket_new(&a);
    emscripten_websocket_set_onopen_callback   (sock_, this, on_open);     // connected_ = true
    emscripten_websocket_set_onmessage_callback(sock_, this, on_message);  // inbox_.push_back(text)
    emscripten_websocket_set_onclose_callback  (sock_, this, on_close);    // connected_ = false
    return sock_ > 0;
}
```

Linked with `-lwebsocket.js` (set by CMake for the Emscripten build). One gotcha:
for text messages Emscripten's `numBytes` **includes the NUL terminator**, so we use
`numBytes - 1` for the string length.

This is the *same* transport the dashboard uses (both ride the browser's native
WebSocket), which is why the dashboard is a faithful demo of the web SDK path.

---

## 5. The `Realtime` handle: framing & parsing

`Realtime` (in `sdk/cpp/src/realtime.cc`) is the transport-agnostic logic. Building
ops is trivial hand-assembled JSON with the SDK's own `json::escape`:

```cpp
void Realtime::join(const string& room) { send_op(R"({"op":"join","room":")" + json::escape(room) + R"("})"); }
void Realtime::send(const string& data) { send_op(R"({"op":"msg","data":")"  + json::escape(data) + R"("})"); }
void Realtime::queue()  { send_op(R"({"op":"queue"})"); }
// leave / cancel similarly
```

`connect()` builds the URL from the client's config + current token, converting the
scheme (`http`→`ws`, `https`→`wss`):

```cpp
std::string url = client_->cfg_.base_url;
if      (url.rfind("https://",0)==0) url.replace(0,5,"wss");
else if (url.rfind("http://", 0)==0) url.replace(0,4,"ws");
url += "/v1/ws?api_key=" + client_->cfg_.api_key + "&token=" + client_->token_;
```

(The api_key `pk_…` and the base64url JWT are already URL-safe, so no percent
encoding.) `connect()` refuses if there is no token yet — realtime requires an
authenticated user.

`update()` (called from `Client::update()` each frame) pumps the transport, parses
each frame into an `RtEvent`, and synthesizes two local events the server doesn't
send: `connected` (first time the transport reports open) and `disconnected` (when
`poll()` returns false). Parsing is defensive — a malformed frame becomes an `error`
event, never a throw:

```cpp
RtEvent parse_event(const string& frame) {
    RtEvent e; e.raw = frame;
    auto j = json::parse(frame);
    if (!j) { e.ev="error"; e.message="malformed frame"; return e; }
    e.ev = (*j)["ev"].as_string(); e.room = (*j)["room"].as_string();
    e.data = (*j)["data"].as_string(); /* ...from/user_id/name/members... */
    return e;
}
```

`poll(RtEvent&)` drains one buffered event from a `std::deque`, returning `false`
when empty. The game loops `while (rt.poll(ev))` each frame.

---

## 6. Testing without a socket: the fake transport

Realtime's *logic* (op framing, event parsing, connected/disconnected synthesis) is
worth testing deterministically — no server, no flake. `test_sdk_realtime.cc`
injects a `FakeWs : IWsTransport` that records sent frames and replays canned server
frames on `poll()`. It proves:

- `connect()` refuses without a token, and otherwise builds
  `ws://127.0.0.1:9999/v1/ws?api_key=pk_test&token=jwt.tok.sig`;
- the first `update()` synthesizes exactly one `connected` event;
- `join`/`msg`/`queue`/`cancel`/`leave` emit the right frames, with `"`-escaping in
  `msg` data;
- a batch of incoming frames parses to `joined`(2 members)/`peer_joined`/`msg`/
  `matched`/`peer_left`, and a garbage frame becomes an `error` (no crash);
- a dropped socket surfaces exactly one `disconnected` and then goes inert;
- `client.realtime()` is lazily created and returns the *same* instance each call.

This is the ponytail "one runnable check" for non-trivial logic — fast, offline,
and it exercises every branch of the parser. The *server* behavior (that these
frames mean what we think) is proven separately by `test_baas_realtime` over a real
WebSocket (ch.63 §9), and by the browser smoke below. Together: SDK logic verified
without a socket; protocol verified with one.

---

## 7. The live demo: the dashboard Realtime console

The dashboard (`baas/web/dashboard.html`) gains a **Realtime** tab. It uses the
browser's native `WebSocket` directly — the same transport as the web SDK — so it is
a genuine end-to-end demo with zero native build:

1. **Connect (guest)** — `POST /v1/auth/guest` with the console's api-key to mint a
   token, then open `wsBase + "/v1/ws?api_key=…&token=…"`.
2. **Join** a room / **Leave** / **Send** a message to the room.
3. **Queue** / **Cancel** for matchmaking — open two browser tabs, queue in both,
   and watch both flip to `matched` with the same room.
4. A live log prints every `→` sent and `←` received frame.

### Verified live

Booting the server and driving two browser WebSockets from the dashboard's origin
produced exactly the ch.63 protocol:

```
A joins "demo"  → A ← {"ev":"joined","members":[{uid:2}],"room":"demo"}
B joins "demo"  → B ← {"ev":"joined","members":[{uid:2},{uid:3}]}
                  A ← {"ev":"peer_joined","user_id":3}
A sends "hello from A"
                → B ← {"ev":"msg","from":2,"data":"hello from A"}
                  A ← (nothing — no self-echo)
```

That is the whole tier — auth-on-upgrade, presence, broadcast, tenant scoping —
demonstrated in a real browser against the real server.

---

## 8. Pitfalls

- **Calling `connect()` before auth.** No token → `connect()` returns false. Order:
  `guest()`/`login()` → wait for the token → `connect()`.
- **Expecting your own broadcasts back.** The server never echoes `msg` to the
  sender. Render your own outgoing messages locally.
- **Assuming native realtime always works.** On a machine without a WS-capable
  libcurl the native transport is a stub (`open()` → false). Check `connected()`.
  The web path is unaffected.
- **Forgetting to pump.** Events only appear after `Client::update()` (which pumps
  the realtime channel too). No update, no events.
- **Reference cycles in test clients.** A WebSocket client whose message handler
  captures a shared_ptr that owns the client is a cycle; the realtime *integration*
  test tolerates this (leak detection off) — production code should hold a
  `weak_ptr` or clear the handler on close.

---

## 9. Glossary

- **`IWsTransport`** — the SDK's WebSocket seam; native (libcurl), web (browser),
  and fake (tests) implementations.
- **CONNECT_ONLY=2** — libcurl mode that does the WS handshake and then hands you
  raw `curl_ws_send`/`curl_ws_recv` instead of running a transfer.
- **Fragmentation** — one logical WS message split across frames; reassembled by
  watching `curl_ws_frame::bytesleft`.
- **Synthesized event** — `connected`/`disconnected`, produced by the SDK from
  transport state (the server never sends them).
- **Keg-only** — a Homebrew package not symlinked onto the default paths; must be
  located explicitly (why the SDK's CMake probes for WS-capable curl).

---

## 10. Exercises

1. **Chat scene.** In colony, on `msg`, append `ev.name + ": " + ev.data` to an
   on-screen chat log. *Hint:* you already drain events each frame; just render.
2. **Presence panel.** Track a set of peer ids from `joined`/`peer_joined`/
   `peer_left` and draw "N players online". *Hint:* seed from `joined.members`.
3. **Auto-reconnect.** On a `disconnected` event, retry `connect()` with backoff.
   What state should the game preserve across the gap?
4. **Binary frames.** Extend `IWsTransport` with `send_binary`/binary delivery for a
   compact state channel. *Hint:* `CURLWS_BINARY` native; `send`/`isText=false` web.
5. **Wire the native path into an integration test.** Now that `gbaas_sdk` links a
   WS-capable curl, write a test that drives the *SDK's* `Realtime` against the live
   server (not Drogon's client). What has to be true of the build environment for it
   to be non-flaky in CI?

---

*This closes the L2 realtime tier. The platform now spans auth, six L1 services, an
operator dashboard, and realtime — REST for state, WebSocket for presence — each
usable from the unified C++ SDK, native and web.*
