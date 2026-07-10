# Chapter 57 — Colony Online: Integrating the SDK

> **What this is.** Wiring the SDK into a real game. The colony scene gains guest
> sign-in, score submission, and a live leaderboard — the same code running native
> and in the browser. You'll see how the non-blocking client fits a scene's
> `update`/`render`, and the neat trick that avoids CORS entirely: the backend
> serves the WASM bundle itself. Code: `src/games/colony/colony_scene.*`,
> `baas/main.cc` (`--static`), `web/shell.html`.

---

## 1. One client, pumped every frame

`ColonyScene` holds a `gbaas::Client` and signs in as a guest in its constructor:

```cpp
ColonyScene::ColonyScene()
    : sim_(14, 14), frame_(64 * 1024),
      client_(gbaas::Config{default_base_url(), "pk_demo_colony"}) {
    …
    login();   // async guest sign-in; completes over the next few frames
}
```

The one rule the SDK imposes is *pump it*, so `update()` drives the client before the
sim — even while paused, so responses still arrive:

```cpp
void ColonyScene::update(double dt, const platform::InputState&) {
    client_.update();          // fire any completed callbacks
    if (!running_) return;
    …
}
```

## 2. Actions are callbacks that touch scene state

Because callbacks run during `update()` (on the game thread), they can mutate scene
fields directly — no locking, no queues:

```cpp
void ColonyScene::submit_score() {
    client_.leaderboard("colony_high").submit(sim_.agent_count(),
        [this](gbaas::Result<gbaas::Rank> r) {
            if (r) { my_score_ = r->value; my_rank_ = r->rank; refresh_board(); }
        });
}
```

The colony's "score" is simply how many colonists it's managing (`agent_count()`),
so spawning agents and submitting climbs the board. The immediate-mode panel
(subsystem F) grew a few controls — status line, `Submit score`, `Leaderboard` — and
the board itself is drawn read-only with `fill_rect` + `draw_text`. The world-click
handler learned to ignore clicks that land on the board panel, the same way it
already ignores clicks on the UI (`!ui_.hovering_ui()`).

Capturing `this` in the callbacks is safe here because the `Client` is a member of
the scene: it never outlives the callbacks' target.

## 3. The CORS trick: let the backend serve the game

A browser blocks a page on origin A from calling an API on origin B unless the API
opts in with CORS headers. The simplest way to *not* need CORS is to put the page and
the API on the **same origin** — so the backend serves the WASM bundle too:

```
baas --static build-web        # setDocumentRoot + wasm/data MIME types
```

Now `http://127.0.0.1:8080/demo.html` and `http://127.0.0.1:8080/v1/auth/guest` are
one origin; the SDK's web base url is `""` (relative), and calls just work. One
subtlety: Drogon serves only an allowlist of extensions and doesn't know `.wasm`, so
we add the bundle's types and register `application/wasm` (browsers need that MIME for
streaming compilation). For a genuinely cross-origin deployment you'd add CORS headers
instead — but for the demo, co-serving is simpler and there's nothing to get wrong.

## 4. One WASM build, any scene

`web/shell.html` picks the scene from the URL — `?mode=colony` runs this demo,
default runs chess — by setting Emscripten's `Module.arguments` (which become
`argv[1..]` in our unchanged `main.cpp`). No recompile to switch demos:

```js
var mode = new URLSearchParams(location.search).get('mode') || 'gui';
var Module = { arguments: argsByMode[mode] || argsByMode.gui, … };
```

## 5. What "it works" looked like

Native: run `baas` (seeded), run `./build/demo --colony`, sign in, submit, watch the
board. Web: build `demo.html` with `emcc`, serve it from `baas --static build-web`,
open `?mode=colony` — the WASM boots the colony scene and its first frame issues
`POST /v1/auth/guest`, which returns **200** over `emscripten_fetch`. Same game code,
same SDK, two transports, one backend.

## 6. Checkpoints

- Why call `client_.update()` even when the sim is paused?
- Explain the same-origin trick. What would you add instead to support a page hosted
  on a different domain than the API?
- How does `?mode=colony` change what runs without recompiling the WASM?
