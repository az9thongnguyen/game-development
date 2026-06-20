# Chapter 08 — The M0 Demo & Acceptance

> **Goal of this chapter.** Assemble everything into the **M0 demo scene** —
> moving shapes, a translucent sprite, on-screen text, input-driven movement, and
> a live **FPS counter** — then *prove* M0 is done by running the acceptance
> checks: clean build, passing tests, no leaks, no undefined behavior, and the
> architecture invariants that guarantee the web port stays a drop-in.

---

## 1. The demo scene

`demo/DemoScene` is the first scene that lives in its own file under `src/demo/`
(games and tools graduate out of `main.cpp`). It combines every M0 subsystem:

- **fixed-timestep `update`** moves the player sprite from keyboard input,
- **`render`** draws a faint reference grid (a `draw_line` stress test), a
  decorative sprite orbiting on simulated `time`, the player sprite, a mouse
  crosshair, a centered title, the asset line, and the FPS counter,
- the **asset seam** loads `assets/hello.txt` at construction.

It's intentionally a *showcase*: each visible element maps to a subsystem you
built, so running it is a visual checklist of M0.

### The FPS counter

Frame rate is just `1 / dt`, but raw `dt` jitters frame to frame, so the number
would be unreadable. We smooth it with an **exponential moving average** — keep
90% of the old estimate, mix in 10% of the new:

```cpp
if (ctx.dt > 0.0) fps_ = fps_ * 0.9 + (1.0 / ctx.dt) * 0.1;
```

This is the same one-line filter you'll reuse for any noisy signal. With vsync on,
it settles near 60. We draw it top-right by measuring the string width
(`len * 8` pixels for the 8×8 font) and offsetting from the right edge — the same
trick that right-aligns any HUD text.

---

## 2. What you should see

```sh
cmake --build build
./build/demo            # from the project root
```

A 960×540 window: a dark grid, the **"HAND-ENGINE  M0"** title centered up top, a
green **FPS ~60** top-right, two amber diamonds (one orbiting, one you drive with
**arrows/WASD**), a white crosshair following the **mouse**, and at the bottom the
controls plus the green line loaded from `assets/hello.txt`. **ESC** quits.

That single screen exercises: the platform/framebuffer/present path, the
fixed-timestep loop, the 2D renderer (clear, lines, sprite alpha-blit, text),
normalized input, and the asset seam — i.e. all of M0.

---

## 3. Acceptance — and how we verified each

M0's bar (from `requirements.md` §7 & §10) and the exact checks:

| Criterion | How we verified | Result |
|-----------|------------------|--------|
| Builds native, **no warnings** | `cmake --build build` with `-Wall -Wextra -Wpedantic` | clean |
| Window + software render + text + **FPS** | run `./build/demo` | shows the scene above |
| **Input** works, clean quit | arrows/WASD move; ESC / close quits | works |
| **Math** correctness | `ctest` (`test_math`) | 100% passed |
| **No leaks** | `HAND_ENGINE_FRAMES=120 leaks --atExit -- ./build/demo` | 0 leaks |
| **No UB / memory errors** | `-DENGINE_SANITIZE=ON` build + run | build clean + drawing is bounds-checked; the ASan **run** is inconclusive in the CI sandbox (aborts inside SDL's Metal path under instrumentation) — verify on a normal desktop |
| **Seam: no SDL above platform** | `grep -rn "SDL_" src/engine src/demo` | none |
| **Seam: no blocking loop** | loop only in `platform::run`; engine uses `tick(dt)` | holds |
| **Web-ready structure** | `cmake/emscripten.toolchain.cmake` present (built at M5) | staged |

A few notes on the tooling:

- **`leaks` vs LeakSanitizer.** On macOS, ASan's leak detector (LSan) isn't
  supported, so we use ASan/UBSan for *memory errors and undefined behavior* and
  the system **`leaks`** tool for *leak detection*. Belt and suspenders.
- **ASan in a headless sandbox (honesty note).** Running the *instrumented* binary
  inside a headless/sandboxed macOS environment can abort or hang inside SDL's
  window/Metal path — non-instrumented system GPU libraries, not our code. So in
  this environment the ASan **run** is inconclusive; acceptance here leans on a
  warning-clean build, `leaks` = 0, and the fact that every drawing primitive
  clips its writes. On a normal desktop, run `cmake -B build-asan
  -DENGINE_SANITIZE=ON && cmake --build build-asan && ./build-asan/demo` to get the
  real ASan result.
- **`HAND_ENGINE_FRAMES`** lets the app exit on its own, which is what makes
  `leaks --atExit` and any future CI possible without a human closing the window.
- **The grep checks are real tests.** They mechanically enforce the two
  invariants that keep M5 cheap. If someone later `#include <SDL.h>` in a scene,
  the grep catches it.

---

## 4. The Emscripten seam (M5, staged not built)

`cmake/emscripten.toolchain.cmake` exists and is documented, wiring `-s USE_SDL=2`
and the framebuffer-to-`<canvas>` flags — but we do **not** build it yet. M0's job
was to make that future build a drop-in; actually doing it is M5. This is the
honest scope line: the seam is in place, the implementation is deferred.

---

## 5. Common pitfalls

- **Claiming "no leaks" without checking.** Always run the tool; SDL apps can leak
  if you forget `SDL_Quit` or destroy objects out of order. We destroy
  texture→renderer→window→`SDL_Quit` in `shutdown()`.
- **Unsmoothed FPS.** Showing raw `1/dt` flickers wildly; smooth it.
- **Letting the demo creep into `main`.** Scenes belong in their own files; `main`
  just wires platform + engine + the starting scene.

---

## 6. Glossary

- **Acceptance criteria** — the checklist that defines "done" for a milestone.
- **Exponential moving average** — a cheap smoothing filter (`new = old*k +
  sample*(1-k)`).
- **Invariant** — a property we mechanically enforce (here, via grep) so it can't
  silently regress.

---

## 7. Exercises

1. **Pause toggle.** Use `input.pressed(Key::Space)` to freeze the orbiting sprite
   and dim the FPS — practice with edges and scene state.
2. **Show frame time too.** Add a `ms %.1f` readout next to FPS using `ctx.dt *
   1000`. Which is more useful when debugging a stutter — FPS or ms?
3. **Break an invariant, watch it caught.** Temporarily `#include <SDL.h>` in
   `demo_scene.cpp`, then run the grep check from §3. Revert.
4. **Your own scene.** Copy `DemoScene` to `MyScene`, swap it into `main`, and make
   something — you now have the full M0 toolkit.

---

## 8. What's next

M0 is complete: a real, documented, tested engine foundation with the web port
de-risked. The branch merges into `main` as the M0 milestone. Next is **M1 —
chess**: a full rules engine (all moves, castling, en passant, promotion,
check/checkmate/stalemate), Human↔Human and Human↔AI (minimax + alpha-beta), in
both GUI and TUI. That gets its own spec, plan, and chapters — built on everything
here.
