# Chapter 33 — A Native Webserver (from scratch)

> **What this is.** An optional extension (`requirements.md` §11): a small C++
> webserver that **serves the WebAssembly build** and a tiny **leaderboard API** —
> written by hand over POSIX sockets, no framework. The whole point is the
> **boundary**: this is a *separate process* that links **none** of the engine, and
> talks to the game only over HTTP. You'll learn the HTTP request/response cycle,
> why `.wasm` needs the right MIME type, the path-traversal trap every static server
> must defend, and how to keep a server that eats untrusted input from falling over.
> Code: `server/` (a sibling of `src/`), built as the `webserver` target.

---

## 1. Why a separate process — the §11 boundary

The engine's iron rule has been "SDL is the only dependency, hand-write the rest."
A webserver is a different *kind* of thing — a distribution/service layer — so
`requirements.md` §11 carves it out explicitly:

> The webserver is a **separate process and build target**. It must not pull any
> dependency or logic into the engine core, and the engine stays web-portable
> exactly as before. The two meet only over **HTTP**, never by linking.

We honor that structurally: a new top-level `server/` tree, a CMake `webserver`
executable that compiles **only `server/*.cpp`** (it links `engine_flags` solely for
warning flags — zero engine sources), and builds **desktop-only** (never part of the
WASM bundle). Nothing in `src/` knows the server exists. You could delete `server/`
and the games would build and run unchanged.

`requirements.md` names *Drogon* as an example framework. We instead **hand-write**
the server — same spirit as the rest of the project, no heavy dependency, and it
teaches the HTTP/socket layer directly instead of hiding it behind a framework.

## 2. What it does

```
   browser ──HTTP──▶ webserver ──┬─▶ static files under --root (the WASM bundle)
                                 │     /demo.html /demo.js /demo.wasm /demo.data
                                 └─▶ /api/scores  (GET list · POST add)  → scores.json
```

Two jobs: **serve the M5 build** so you can open the game at `http://localhost:8080/`,
and a **leaderboard API** as a concrete example of the game↔service boundary (the
WASM game could `fetch('/api/scores')`).

## 3. The shape: logic vs. sockets (the same seam idea)

Just like the engine isolates SDL behind `platform.hpp`, the server isolates the OS
networking behind one file. Everything else is pure and unit-tested:

```
server/
  http.{hpp,cpp}        parse a request / serialize a response   (no sockets → tested)
  mime.hpp              extension → Content-Type
  static_files.{hpp,cpp} URL → safe file path + read             (no sockets → tested)
  leaderboard.{hpp,cpp} hand-written JSON, persistence, sanitize (no sockets → tested)
  net.{hpp,cpp}         the ONLY file with sockets: accept loop
  main.cpp              routing + args
```

`tests/test_server.cpp` (ctest `server`) drives the four pure modules **without ever
opening a port** — it doesn't even compile `net.cpp`. Same discipline that kept the
chess rules and the A* planner testable.

## 4. HTTP is just text over a socket

An HTTP/1.1 request is a few lines of ASCII followed by an optional body:

```
POST /api/scores HTTP/1.1\r\n      ← request line: METHOD TARGET VERSION
Host: localhost\r\n                ← headers, one per line
Content-Length: 23\r\n
\r\n                               ← blank line ends the headers
{"name":"AB","score":7}            ← body (Content-Length bytes)
```

`parse_request` (in `http.cpp`) splits the header block from the body at the blank
line, tokenizes the request line into exactly three parts (reject otherwise), and
reads `Name: value` headers into a list with case-insensitive lookup. A response is
the mirror image, built by `serialize`:

```
HTTP/1.1 200 OK\r\n
Content-Type: application/wasm\r\n
Content-Length: 8143463\r\n
Connection: close\r\n
\r\n
<bytes…>
```

`Content-Length` must match the body exactly (the client reads precisely that many
bytes); `Connection: close` tells the client we serve one request per connection and
then hang up — the simplest correct model.

## 5. The accept loop (`net.cpp` — the one socket file)

The classic five calls: `socket → setsockopt(SO_REUSEADDR) → bind → listen →
accept`, then loop:

```cpp
for (;;) {
    int c = accept(s, ...);
    // read a BOUNDED request, parse, route via handler, serialize, write, close.
}
```

The reusable lesson is that a server eats **untrusted input**, so the loop is written
defensively (each of these is a real fix from the security review):

- **Bounded read.** `read_request` stops at 1 MiB and reads exactly
  `Content-Length` body bytes — no unbounded-memory DoS, no overflow in the size math.
- **Receive timeout.** `SO_RCVTIMEO` (5 s) means a client that dribbles one byte at a
  time can't freeze the single-threaded loop forever (a *slow-loris*).
- **Survive transient errors.** `accept` returning `ECONNABORTED`/`EMFILE` is
  recoverable — `continue`, don't exit. (The naive `break` lets one port-scanner RST
  kill the server.)
- **No fd leak, no crash on `bad_alloc`.** The handler runs in a `try{…}catch(...)`
  and `close(c)` is *outside* the try, so one failed request never leaks a socket or
  takes the process down.
- **Ignore `SIGPIPE`.** Writing to a client that hung up would otherwise kill us with
  a signal; `signal(SIGPIPE, SIG_IGN)` turns it into a normal `send` error.
- **Bind localhost by default** so the server isn't exposed to the LAN unless you ask
  (`--host 0.0.0.0`).

## 6. Serving files safely: MIME and the traversal trap

### `.wasm` must be `application/wasm`

Browsers *streaming-compile* WebAssembly, and they refuse to if the bytes arrive with
the wrong `Content-Type`. `mime_for` maps the extension; the one that matters is
`.wasm → application/wasm`. Get it wrong and the page fails to start with a console
error — nothing renders.

### Path traversal — the #1 static-server bug

A request for `/../../etc/passwd` (or its encoded cousin `/%2e%2e/...`) must **not**
escape the web root. `resolve` defends in the right order:

```cpp
path = percent_decode(target_without_query);   // 1) decode FIRST…
// 2) …THEN reject: any ".." segment, backslash, or NUL byte
if (has_unsafe_segment(path)) return std::nullopt;   // → 403
return root + "/" + path;                            // 3) only now join to root
```

Decoding **before** the check is essential: otherwise `%2e%2e` slips past a naive
`..` filter and *then* becomes `..` on the filesystem. We split on `/` and reject any
`..` segment outright. (Verified live: both `/../CMakeLists.txt` and
`/%2e%2e/CMakeLists.txt` return **403**.)

## 7. The leaderboard: the game↔service boundary

`leaderboard.cpp` keeps a top-100 list, persisted to `scores.json` as hand-written
JSON (same "write your own format" habit as the `.hrt` loader and the farm
serializer — no JSON library):

- `GET /api/scores` → the list, highest first.
- `POST /api/scores` with `{"name":"…","score":N}` → add, sort, **save**, return the
  list.

Two safety habits worth copying:

- **Sanitize input you'll emit.** `sanitize_name` keeps only printable ASCII, drops
  `"` and `\`, and caps the length (24 chars), so a crafted name can't break out of the
  JSON string, inject fields, or be unbounded — the response is always well-formed.
- **Validate numbers.** The score parse rejects overflow (`errno == ERANGE`) *and*
  clamps the range (`|v| ≤ 1e9`), so `{"score":99999999999999999999}` (and merely absurd
  values) are rejected (400) instead of landing `LONG_MAX` at the top of the board.

## 8. Run & observe

```sh
cmake --build build --target webserver
./build/webserver --root build-web        # serves the M5 WASM build
# open http://localhost:8080/  → the game runs, delivered by your own server
```

Try the API with `curl`:

```sh
curl -i localhost:8080/demo.wasm | head -3          # Content-Type: application/wasm
curl -X POST -d '{"name":"ACE","score":99}' localhost:8080/api/scores
curl localhost:8080/api/scores                       # [{"name":"ACE","score":99}]
curl --path-as-is -o /dev/null -w '%{http_code}\n' localhost:8080/../CMakeLists.txt  # 403
```

Args: `--root DIR` (default `build-web`), `--port N` (8080), `--host IP`
(127.0.0.1), `--scores FILE` (scores.json).

## 9. Pitfalls

- **Wrong `.wasm` MIME** → the game won't start; check `Content-Type`.
- **Decoding after the traversal check** → encoded `..` bypass. Decode first.
- **Blocking on a slow client** → add a recv timeout (we do).
- **`break`ing the accept loop on a recoverable error** → a single bad client kills
  the server. `continue` on transient errno.
- **Reflecting request data into headers** → response splitting. We never do (headers
  come only from hardcoded strings + the MIME table).
- **Pulling the server into the engine** → don't. It's a separate target by design;
  the only link is HTTP.

## 10. Glossary

- **Static file server** — returns files from a root directory by URL path.
- **MIME / Content-Type** — the `type/subtype` telling the browser how to treat bytes.
- **Path traversal** — escaping the web root via `..`; the canonical static-server bug.
- **Slow-loris** — a DoS that holds connections open by sending data very slowly.
- **`SO_REUSEADDR` / `SO_RCVTIMEO`** — socket options: rebind quickly after restart /
  bound how long a recv blocks.
- **Service boundary** — the HTTP line between the (WASM) game and this server; no
  shared code crosses it.

## 11. Exercises

1. **Wire the game to the API.** From `web/shell.html`, `fetch('/api/scores')` and
   show a leaderboard panel beside the canvas. Notice nothing in `src/` changes.
2. **Concurrency.** Handle each connection on its own thread (or `poll`/`epoll`), so
   one slow client never blocks others. What new bugs does shared state invite?
3. **ETag / caching.** Add `Last-Modified`/`ETag` headers and honor
   `If-None-Match` with `304 Not Modified` to skip re-sending the big `.wasm`.
4. **A second endpoint.** Add `DELETE /api/scores` (clear the board) and a tiny token
   check — the first taste of auth, still entirely outside the engine.

---

### Where this leaves the project

With M0–M5 complete and this optional service layer added, the full arc of
`requirements.md` is realized: a hand-written C++ engine (SDL only at the seam, every
pixel ours), five games/tools, a WebAssembly port with no rewrite, and — cleanly
separated behind HTTP — a from-scratch server to distribute it and host an online
feature. Every layer was built to be understood, which was the whole point.
