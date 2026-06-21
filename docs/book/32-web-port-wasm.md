# Chapter 32 — M5: The WebAssembly Port

> **What this is.** The capstone. We take the *exact same* engine and games and run
> them in a browser via **WebAssembly (WASM)** — no rewrite of any engine or game
> code. The entire port is: one `#ifdef` in the platform's `run()` loop, a CMake
> branch, a 40-line HTML shell, and `emcc`. This chapter explains *why* it was that
> small — the answer is every design decision from Chapter 00 onward — and exactly
> how to build and verify it. Files: `src/platform/backend_sdl.cpp` (the `#ifdef`),
> `CMakeLists.txt` (the `EMSCRIPTEN` branch), `web/shell.html`,
> `cmake/emscripten.toolchain.cmake`.

---

## 1. What WebAssembly is, and why this works at all

**WebAssembly** is a compact binary instruction format that every modern browser runs
at near-native speed in a sandbox. **Emscripten** is a toolchain (`emcc`, a clang
front-end) that compiles C/C++ to WASM and provides browser implementations of the
POSIX-ish APIs your program expects: a virtual filesystem, `malloc`, `stdio`, and —
crucially for us — a **port of SDL2** that maps SDL calls onto an HTML `<canvas>`,
the Web Audio API, and DOM input events.

That last point is the whole game. Our platform layer already speaks SDL2. Emscripten
*also* speaks SDL2. So 95% of `backend_sdl.cpp` compiles and runs on the web
unchanged. There is exactly **one** thing SDL-on-the-web can't do the way the desktop
does: block in an infinite loop. Everything else is build plumbing.

## 2. The one real difference: you may not block the browser

A desktop game owns its thread and spins:

```cpp
while (!quit) { pump(); frame(dt); present(); }   // we own this thread; fine
```

A web page does **not** own the thread — the browser's event loop does. If you block it
with a `while(true)`, the tab freezes: no rendering, no input, no network, eventually
a "page unresponsive" dialog. Browsers render by calling *you back* once per frame
(`requestAnimationFrame`). Emscripten exposes that as:

```cpp
emscripten_set_main_loop(callback, fps, simulate_infinite_loop);
```

You hand it a per-frame callback and **return control to the browser**. This is the
entire reason Chapter 03 insisted the loop live in `platform::run` behind a `tick(dt)`
callback instead of a `while` in the engine. We are now cashing in that decision.

### The `#ifdef` (the only engine-side change in all of M5)

```cpp
void run(const std::function<void(double)>& frame) {
#ifdef __EMSCRIPTEN__
    g_frame = frame;
    g_prev  = SDL_GetPerformanceCounter();
    g_freq  = static_cast<double>(SDL_GetPerformanceFrequency());
    emscripten_set_main_loop(emscripten_tick, 0, 1);   // browser drives us now
#else
    while (!g_quit) { pump_events(); /* dt … */ frame(dt); present(); }
#endif
}
```

`emscripten_tick` is a tiny trampoline that does what one iteration of the desktop
loop did — `pump_events()`, compute `dt`, `g_frame(dt)`, `present()` — using a stored
copy of the callback:

```cpp
void emscripten_tick() {
    pump_events();
    double dt = (SDL_GetPerformanceCounter() - g_prev) / g_freq;
    g_prev = SDL_GetPerformanceCounter();
    if (dt > 0.25) dt = 0.25;        // same spiral-of-death clamp as App::frame
    g_frame(dt);
    present();
    if (g_quit) emscripten_cancel_main_loop();
}
```

The arguments `(callback, 0, 1)`: `fps = 0` means "use `requestAnimationFrame`" (sync
to the display, ~60 Hz); `simulate_infinite_loop = 1` means the call **does not
return** — Emscripten unwinds the C++ stack and keeps invoking the callback. (A side
effect: `platform::shutdown()` after `run()` never executes on the web. That's fine —
the browser reclaims everything on page close.)

**That is the entire engine-side port.** `App`, every scene, the renderers, the math,
chess, the 3D rasterizer, the iso sim — none of them changed a single character.

## 3. Why the other invariants paid off

Three more Chapter-00 rules made the port a drop-in rather than a slog:

- **No SDL above the platform layer.** Because only `backend_sdl.cpp` touches SDL,
  there was exactly one file to think about for the platform difference. If SDL calls
  were sprinkled through the engine, each would have been a porting hazard.
- **All file I/O behind the `assets` seam.** Emscripten can't read your disk; it serves
  files from a **virtual filesystem** preloaded at build time. Because every read goes
  through `assets::load_file` (plain `std::ifstream`, which Emscripten implements over
  its VFS), the preloaded files "just work". Chess's piece sprites load on the web
  through the identical code path as on desktop.
- **Single-threaded.** Web threads need cross-origin isolation headers and special
  flags. We never assumed threads, so there was nothing to untangle.

## 4. The build plumbing

### CMake `EMSCRIPTEN` branch

`emcmake cmake` sets the `EMSCRIPTEN` variable and points CMake at Emscripten's
toolchain. Our `CMakeLists.txt` then takes a different path for SDL and output:

```cmake
if(EMSCRIPTEN)
  add_compile_options(-sUSE_SDL=2)                       # SDL2 from Emscripten's port
  add_link_options(-sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1
                   "--preload-file=${CMAKE_SOURCE_DIR}/assets@assets"  # VFS
                   "--shell-file=${CMAKE_SOURCE_DIR}/web/shell.html")  # our HTML
  set(CMAKE_EXECUTABLE_SUFFIX ".html")                   # emit demo.html+.js+.wasm
  set(SDL2_LINK "")                                      # no find_package on web
else()
  # …desktop: find_package(SDL2) / pkg-config (unchanged)…
endif()
```

Key flags:
- **`-sUSE_SDL=2`** — link Emscripten's SDL2 port (both compile and link).
- **`--preload-file assets@assets`** — bundle the `assets/` dir into a `.data` file
  the runtime mounts at `/assets`, so `assets::load_file("pieces.hrt")` resolves.
- **`--shell-file web/shell.html`** — wrap the program in *our* page (next section).
- **`CMAKE_EXECUTABLE_SUFFIX ".html"`** — make the `demo` target emit
  `demo.html`, `demo.js`, `demo.wasm`, `demo.data`.

We also had to wrap the desktop SDL discovery in `else()` — on the web there is no
`pkg-config sdl2`, so the old unconditional `find_package` would have failed configure.

### The HTML shell

`web/shell.html` is a minimal page with a `<canvas id="canvas">` (where SDL draws) and
a `Module` object. The one app-specific line is:

```js
var Module = {
  arguments: ['--gui', 'hvai', 'medium'],   // becomes argv[1..] in main.cpp
  canvas: document.getElementById('canvas'),
  …
};
```

`Module.arguments` are handed to `main(argc, argv)` — so the **unchanged** argv
dispatch in `main.cpp` picks the scene. Want the 3D core instead of chess? Change the
array to `['--3d']`. The engine binary is identical; only the page's argument list
differs. That is the cleanest possible expression of "no rewrite".

### The toolchain hook

`cmake/emscripten.toolchain.cmake` exists so the build is self-describing; in practice
you use `emcmake`, which configures the real Emscripten toolchain for you.

## 5. Run & verify

One-time SDK install:

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
source ~/emsdk/emsdk_env.sh        # puts emcc/emcmake on PATH (per shell)
```

Build and serve (WASM must be served over **http**, not `file://`):

```sh
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-web
cmake --build build-web --target demo
cd build-web && python3 -m http.server 8765
# open http://localhost:8765/demo.html
```

You should see the chess board render in the canvas and "White to move" in the HUD. (The
page `<title>` stays "hand-engine — WebAssembly" — that's the shell's static title;
`cfg.title` only names the *native* SDL window, not the browser tab.) Switch
`Module.arguments` to `['--3d']`
and reload: the hand-written software rasterizer spins a flat-shaded cube and a UV
sphere over a grid — **the 3D pillar, running in a browser, every pixel still drawn by
our code into a CPU framebuffer that we blit to the canvas.**

> This chapter's milestone was verified exactly this way: built with `emcc`, served
> locally, and driven in Chrome — chess *and* the 3D core both confirmed rendering.

## 6. How `present()` reaches the canvas

Worth making explicit, because it's the satisfying part. On desktop, `present()` uploads
our `uint32_t` framebuffer to an `SDL_Texture` and `SDL_RenderCopy`s it to the window.
On the web, the **same** `present()` runs — but Emscripten's SDL2 backs that texture/
renderer with WebGL on the `<canvas>`. We did not write a line of WebGL. The "push a
CPU framebuffer to the screen" abstraction we chose at M0 maps cleanly onto the canvas,
so 2D and 3D port together (they share one framebuffer — Chapter 19).

## 7. Pitfalls

- **Blocking the loop.** Any `while(true)` above the platform would freeze the tab.
  The `tick(dt)` design forbids it structurally.
- **`file://`.** Browsers refuse to `fetch` the `.wasm`/`.data` from `file://`. Serve
  over http (any static server).
- **Persistence.** Emscripten's default FS is in-memory and vanishes on reload, so the
  iso sim's F5 *save* writes to a volatile MEMFS on the web. Real persistence needs
  IDBFS mounted + an explicit sync — isolated to `assets::write_file` by design, so it's
  a one-function change, not an engine change (Chapter 30).
- **Audio autoplay.** Browsers block audio until a user gesture; the Web Audio context
  resumes on first click. Expect silence until you interact.
- **`set but not used`.** The `HAND_ENGINE_FRAMES` test hook is read only by the desktop
  loop, so it's `#ifndef __EMSCRIPTEN__`'d out to keep the web build warning-clean.

## 8. Glossary

- **WebAssembly (WASM)** — portable binary code browsers run at near-native speed.
- **Emscripten / `emcc`** — clang-based toolchain compiling C/C++ to WASM with browser
  shims (SDL2, a virtual FS, stdio).
- **`emscripten_set_main_loop`** — registers a per-frame callback so the browser, not
  you, owns the loop.
- **Preload / virtual filesystem** — files bundled at build time and mounted in memory,
  read through the normal file API.
- **Shell file** — the HTML template Emscripten wraps the program in (canvas + `Module`).

## 9. Exercises

1. **Scene picker.** Add buttons to `shell.html` that set `Module.arguments` and reload,
   so users can choose chess / fps / 3d / iso without editing files.
2. **Persistent saves.** Mount IDBFS at a save directory, call `FS.syncfs` after
   `write_file`, and make the iso sim's F5/F9 survive a reload. Touch only
   `assets`/the backend — prove the game code stays unchanged.
3. **Resize to fit.** Make the canvas track the window size and pass the new dimensions
   into the framebuffer (careful: the renderers assume a fixed size today).
4. **Size budget.** Inspect `demo.wasm`'s size; try `-Oz` and `-sENVIRONMENT=web` and
   measure the shrink. What did you trade away?

---

### Milestone close

M5 completes the original vision of `requirements.md`: a hand-written C++ engine,
SDL confined to a thin platform shim, every pixel drawn by us — now shipping to the
**web with no engine or game rewrite**, exactly as the architecture promised at M0.
The remaining ideas (a Drogon webserver, more games, richer tools) are extensions on a
foundation that is now proven end-to-end, desktop *and* browser.
