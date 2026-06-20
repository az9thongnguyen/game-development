# Native Webserver — Design Spec

> Date: 2026-06-21 · Optional extension (requirements.md §11) · Branch `feat/webserver`

## 1. Goal & the hard boundary

Add a **native C++ webserver** that serves the WebAssembly build (and a tiny online
feature) — as a **completely separate process and build target**. Per
`requirements.md` §11, the iron rule:

> The webserver is a separate distribution/service layer. It MUST NOT pull any
> dependency or logic into the engine core, and the engine must stay web-portable
> exactly as before. The two connect only over HTTP, never by linking.

So: a new top-level `server/` tree, a new CMake executable `webserver` that links
**nothing** from the engine/games, builds **only on desktop** (never under
Emscripten), and shares no headers with `src/`.

`requirements.md` §11 names Drogon only as an *example*. We instead **hand-write** a
minimal HTTP server over POSIX sockets — no third-party dependency — matching the
project's "learn by building, minimal deps" ethos. (Drogon would mean a heavy install
and a big framework dependency, against that ethos; a small from-scratch server
teaches the HTTP/socket layer directly.)

## 2. Scope

1. **Static file serving** of the WASM bundle: serve `demo.html`, `demo.js`,
   `demo.wasm`, `demo.data`, and any other files under a configurable web root
   (default `build-web/`), with correct **MIME types** (notably
   `application/wasm`). `/` serves `demo.html`.
2. **A tiny JSON leaderboard API** (the "online feature" from §11), to show the
   game↔service boundary:
   - `GET  /api/scores` → JSON array of `{name, score}`, highest first.
   - `POST /api/scores` with body `{"name":"...","score":N}` → add + persist + return
     the updated list.
   - Persisted to a plain JSON file (`scores.json`) so it survives restarts.

**Non-goals:** HTTPS/TLS, auth, concurrency at scale, chunked/streaming, keep-alive
pipelining, Windows sockets. This is a local/dev distribution server.

## 3. Architecture (server/ — zero engine coupling)

```
server/
  http.{hpp,cpp}        Request/Response structs; parse an HTTP/1.1 request from a
                        byte buffer; serialize a response. PURE (no sockets) → testable.
  mime.hpp              extension → Content-Type map (header-only).
  static_files.{hpp,cpp}  resolve a URL path to a file under a root; path-traversal
                        guard; read bytes. PURE of sockets → testable.
  leaderboard.{hpp,cpp} in-memory scores + load/save JSON + a minimal JSON parser for
                        the POST body. PURE → testable.
  net.{hpp,cpp}         the ONLY file with POSIX sockets: listen/accept loop, read a
                        bounded request, hand bytes to http::parse, write the response.
  main.cpp              args (--root, --port, --scores); wire routes; run the loop.
tests/test_server.cpp   mime, path-traversal guard, http parse, leaderboard round-trip.
```

The split mirrors the engine's discipline: all logic (parsing, routing helpers,
leaderboard) is **socket-free and unit-tested**; only `net.cpp` touches the OS.

## 4. HTTP handling (`http.{hpp,cpp}`)

```cpp
struct Request  { std::string method, target, version;
                  std::vector<std::pair<std::string,std::string>> headers;
                  std::string body; };
struct Response { int status = 200; std::string reason = "OK";
                  std::string content_type = "text/plain";
                  std::vector<uint8_t> body; };

std::optional<Request> parse_request(const std::string& raw);  // request line+headers+body
std::vector<uint8_t>   serialize(const Response&);             // status line+headers+body
```

`parse_request` splits on CRLF: method/target/version, then headers until a blank
line, then the body (using `Content-Length` if present). Defensive: reject malformed
request lines; ignore unknown headers; cap sizes (see §7). `serialize` writes
`HTTP/1.1 <status> <reason>`, `Content-Type`, `Content-Length`, `Connection: close`,
blank line, body.

## 5. Static files (`static_files.{hpp,cpp}`)

```cpp
// Map a URL path to a safe absolute file path under `root`, or nullopt if it escapes.
std::optional<std::string> resolve(const std::string& root, const std::string& url_path);
```

Guards (path-traversal is the #1 static-server bug):
- strip a leading `/`; map `""`/`"/"` → `index` (`demo.html`).
- reject any path containing `..` segments, a leading `/` after strip (absolute), a
  backslash, or a NUL byte.
- only then join to `root`. The MIME type comes from the extension via `mime.hpp`.

## 6. Leaderboard (`leaderboard.{hpp,cpp}`)

```cpp
struct Score { std::string name; long value; };
class Leaderboard {
    std::vector<Score> scores_;            // kept sorted desc, capped (e.g. top 100)
  public:
    void add(const std::string& name, long value);   // insert, sort desc, cap
    std::string to_json() const;                      // [{"name":..,"score":..},…]
    bool load(const std::string& path);               // via std::ifstream
    bool save(const std::string& path) const;
};
// Minimal parser for the POST body {"name":"...","score":N}:
bool parse_score_body(const std::string& body, std::string& name, long& value);
```

The JSON is hand-written (the project hand-writes formats — cf. the `.hrt` loader and
the farm serializer). `name` is **sanitized** on input: cap length, strip control
chars and `"`/`\` so the emitted JSON can't be broken or injected.

## 7. Networking & safety (`net.{hpp,cpp}`)

A blocking accept loop, one connection at a time, `Connection: close`:

```
socket → setsockopt(SO_REUSEADDR) → bind(port) → listen
loop: fd = accept(); read request (bounded); route; write response; close(fd)
```

This server consumes **untrusted network input**, so defensiveness is mandatory even
for a dev tool:
- **Bounded read:** stop reading a request after a max size (e.g. 1 MiB) → no
  unbounded-memory DoS. Read headers, then exactly `Content-Length` body bytes
  (also capped).
- **Path traversal:** the `resolve` guard above; serve only under `root`.
- **No shell, no exec, no SQL:** pure file reads + an in-memory list.
- **Input sanitizing:** leaderboard `name` cleaned before storage/emission.
- **Bind to localhost by default** (`127.0.0.1`) so it isn't exposed to the LAN
  unless asked.

## 8. Routing (`main.cpp`)

```
POST /api/scores  → parse body → leaderboard.add → save → 200 JSON (updated list)
GET  /api/scores  → 200 JSON
GET  <anything>   → static file under root (200 + MIME) or 404
other method      → 405
```

Args: `--port N` (default 8080), `--root DIR` (default `build-web`), `--scores FILE`
(default `scores.json`). Prints the URL on start.

## 9. Build (CMake)

```cmake
if(NOT EMSCRIPTEN)                      # native only; never part of the web build
  add_executable(webserver server/main.cpp server/http.cpp server/static_files.cpp
                           server/leaderboard.cpp server/net.cpp)
  target_include_directories(webserver PRIVATE server)
  target_link_libraries(webserver PRIVATE engine_flags)   # warnings only, NO engine code
endif()
add_executable(test_server tests/test_server.cpp server/http.cpp
               server/static_files.cpp server/leaderboard.cpp)   # no net.cpp → no sockets
add_test(NAME server COMMAND test_server)
```

`webserver` links `engine_flags` **only for the warning flags** — it pulls in no
engine sources. `test_server` omits `net.cpp` so tests never open a socket.

## 10. Verification

- `ctest` `server` suite: MIME lookup; path-traversal rejection (`..`, absolute,
  backslash, NUL); HTTP parse (good + malformed); leaderboard add/sort/cap, JSON
  round-trip, body parse + sanitization.
- Live: run `./build/webserver --root build-web`, then `curl`:
  - `curl -I localhost:8080/demo.wasm` → `Content-Type: application/wasm`.
  - `curl localhost:8080/demo.html` → the shell HTML.
  - `curl -X POST -d '{"name":"AAA","score":42}' localhost:8080/api/scores` then
    `curl localhost:8080/api/scores` → contains the new score.
  - `curl localhost:8080/../CMakeLists.txt` → 404 (traversal blocked).
- (Optional) point the WASM page's leaderboard at the API — left as a doc note /
  exercise; the boundary is HTTP, nothing in `src/` changes.

## 11. Guidebook

One chapter (appended after the web-port chapter): HTTP from scratch (sockets, the
request/response cycle, MIME, serving WASM), the leaderboard as the game↔service
boundary, the path-traversal lesson, and **why this stays a separate process** —
re-stating the §11 invariant. Update `00-overview` + `README`.

## 12. Risks / decisions

- **Untrusted input:** mitigated by bounded reads, the traversal guard, input
  sanitizing, and a security review pass before merge.
- **Blocking single-connection loop:** fine for local/dev; concurrency is an
  exercise. Documented honestly.
- **POSIX-only sockets:** macOS/Linux; Windows out of scope (documented).
- **Coupling:** enforced by build structure (separate target, no engine sources) and
  a grep check that `server/` includes nothing from `src/`.
