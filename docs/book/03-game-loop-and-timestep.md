# Chapter 03 — The Game Loop & Fixed Timestep

> **Goal of this chapter.** Understand the heartbeat of every real-time game: the
> loop. We'll see why the obvious "move by time-since-last-frame" approach is
> subtly broken, fix it with a **fixed-timestep accumulator** (tracing the numbers
> by hand), and meet the `Scene`/`App` structure every game in this project plugs
> into. We'll also explain *why the loop lives in the platform layer* — the single
> trick that makes the web port a drop-in.

---

## 1. What a game loop is

Strip any real-time game to its skeleton and you get one loop, run forever:

```
   ┌────────────────────────────────────────────┐
   │  read input → advance the world → draw it   │  ← one "frame"
   └───────────────▲────────────────────────────┘
                   └──────────── repeat ─────────┘
```

One trip around is a **frame**. On a 60 Hz monitor that's ~60 times a second, so
each frame has ~16.7 ms to do everything. The central question of this chapter is
deceptively simple: **how much should the world advance each frame?**

---

## 2. The obvious answer, and why it's a trap

The intuitive approach — a **variable timestep** — measures real elapsed seconds
`dt` and scales motion by it: `position += velocity * dt`. It looks correct, and
for slow-moving UI it's fine. But it has two real failure modes.

**Failure 1 — non-determinism.** Floating-point arithmetic isn't associative:
adding `velocity * 0.016` sixty times does **not** give exactly the same result as
adding `velocity * 0.0166…` a different number of times. So the *same inputs*
produce *different* outcomes at 60 Hz vs 144 Hz, or whenever the frame rate
wobbles. For chess that's harmless; for physics, collision, replays, and
networked play it's a debugging nightmare — "it only desyncs on his machine".

**Failure 2 — tunnelling on a hitch.** Say a bullet moves 1000 px/s and a wall is
20 px thick. Normally `dt ≈ 0.016`, so the bullet steps ~16 px/frame and hits the
wall. Now you drag the window and one frame takes `dt = 0.5 s`: the bullet jumps
`1000 * 0.5 = 500 px` in a single step and **passes straight through the wall** —
no collision ever tested at the in-between positions.

Both failures share a root cause: *logic is being driven by an unpredictable,
variable `dt`.*

---

## 3. The fix: a fixed timestep with an accumulator

Decouple two clocks:

- **Logic** advances only in fixed, equal chunks — always `FIXED_DT = 1/60 s`.
- **Rendering** happens once per frame, as often as the display allows.

Keep an **accumulator** of real time that hasn't been simulated yet. Each frame,
add the real `dt`, then spend it in whole `FIXED_DT` chunks:

```
accumulator += dt
while accumulator >= FIXED_DT:      # runs 0, 1, or several times
    update(FIXED_DT)                # logic ALWAYS sees the same dt
    accumulator -= FIXED_DT
render()                            # draw once, using leftover for smoothing
```

### Trace it by hand

`FIXED_DT = 1/60 ≈ 0.01667 s`. Suppose frames arrive with uneven real `dt`:

| Frame | real `dt` | accumulator before | updates run | accumulator after | `alpha` (acc/FIXED_DT) |
|------:|----------:|-------------------:|:-----------:|------------------:|----------------------:|
| 1 | 0.020 | 0.0200 | **1** | 0.0033 | 0.20 |
| 2 | 0.010 | 0.0133 | **0** | 0.0133 | 0.80 |
| 3 | 0.030 | 0.0433 | **2** | 0.0100 | 0.60 |

After 3 frames, real time elapsed = `0.060 s`; updates ran `1+0+2 = 3` times =
`3 × 0.01667 = 0.050 s` of simulated time, with `0.010 s` still waiting in the
accumulator for next frame. Logic *always* saw exactly `0.01667` — deterministic —
while rendering kept pace with the display. This is the classic pattern from Glenn
Fiedler's "Fix Your Timestep!", and it's the engine's heartbeat.

### Refinement 1 — clamp the spiral of death

If one frame is huge (you dragged the window for 2 s), the accumulator becomes
`2.0`, and the `while` loop tries to run `2.0 / 0.01667 ≈ 120` updates in one
frame. That makes the frame *even slower*, which makes the next accumulator *even
bigger* — a runaway called the **spiral of death**. We cap the incoming `dt`:

```cpp
if (dt > 0.25) dt = 0.25;   // simulate at most ~15 steps to catch up, then move on
```

The game appears to "pause and resume" after a stall instead of freezing forever.

### Refinement 2 — `alpha` for smooth rendering (used later)

After the loop, `accumulator / FIXED_DT` is a fraction in `[0, 1)`: how far we are
*between* the last simulated step and the next. A renderer can interpolate a
moving object as `pos = lerp(prev_pos, curr_pos, alpha)` so motion looks buttery
even though logic ticks at a fixed rate. M0 doesn't need it, but we pass `alpha`
in the `Context` so M3 can use it for free.

---

## 4. Why the loop lives in the *platform* layer

Here's the non-obvious payoff. A normal desktop loop blocks:

```cpp
while (!quit) { ...one frame... }   // never returns until you quit
```

**The browser forbids this.** A web page runs on a single cooperative thread that
also handles rendering, events, and layout. If your code never returns, the page
*freezes* — no repaint, no clicks, the dreaded "page unresponsive". Emscripten's
answer is inversion of control: you hand the browser *one frame function* and it
calls you back each animation frame via `emscripten_set_main_loop`.

So we put the loop **mechanism** in the platform backend and have it call a
`frame(dt)` callback:

```
   platform::run(frame)          engine/game just provides `frame`
      │
      ├─ desktop backend:  while(!quit){ pump; dt=...; frame(dt); present; }
      └─ web backend (M5): emscripten_set_main_loop( call frame once per rAF )
```

Same `run` signature, two mechanisms. The engine and games only ever supply a
`frame` callback, so swapping desktop ↔ web changes **nothing** above the platform
layer. Meanwhile the fixed-timestep *logic* lives in the engine (`App::frame`),
because that's game behavior, not platform plumbing.

> This is why you will **never** see a `while(true)` in engine or game code in this
> project. It isn't a style preference — a high-level blocking loop would make the
> web port impossible without a rewrite. (At M5 there's one small wrinkle:
> `emscripten_set_main_loop` wants a C function pointer, so the web backend stashes
> the `std::function` in a static and calls it from a tiny trampoline. That detail
> stays entirely inside the backend.)

---

## 5. The code

### `scene.hpp` — Scene + Context

A **Scene** is one screen: the M0 demo, the chess board, a menu. It exposes two
methods:

```cpp
virtual void update(double dt);              // fixed-step logic (default: nothing)
virtual void render(const Context& ctx) = 0; // draw one frame (required)
```

Splitting `update` (advance the simulation) from `render` (draw it) is exactly the
two-clocks idea in code: `update` is what the accumulator calls in fixed chunks;
`render` is what runs once per frame.

**Context** bundles everything a scene needs to draw, passed by const-ref instead
of reaching for globals:

```cpp
struct Context {
    platform::Framebuffer fb;    // where to draw
    double dt;                   // real seconds since last render (for effects)
    double time;                 // total simulated seconds
    double alpha;                // [0,1) interpolation factor
};
```

Passing a small bundle keeps scenes self-contained and testable — you could call
`render` with a hand-made `Context` in a test.

### `app.hpp` / `app.cpp` — the App

`App` owns the current scene plus the accumulator and total time. Its whole job is
`App::frame(dt)`, which is the accumulator pattern verbatim:

```cpp
if (dt > 0.25) dt = 0.25;                  // clamp (spiral of death)
accumulator_ += dt;
while (accumulator_ >= kFixedDt) {         // kFixedDt = 1.0/60.0
    scene_->update(kFixedDt);
    accumulator_ -= kFixedDt;
    time_        += kFixedDt;
}
Context ctx{ platform::framebuffer(), dt, time_, accumulator_ / kFixedDt };
scene_->render(ctx);
```

It reaches the framebuffer through `platform::framebuffer()` — once again, the only
dependency is the platform interface. The active scene is held in a
`std::unique_ptr<Scene>`, so when `App` is destroyed the scene is cleaned up
automatically (RAII — no manual `delete`).

### `main.cpp` — wiring it together

```cpp
engine::App app(std::make_unique<ColorScene>());
platform::run([&app](double dt) { app.frame(dt); });
```

`platform::run` drives the loop and calls our lambda each frame; the lambda
forwards to `App::frame`, which advances + renders the scene. `ColorScene` is a
throwaway that cycles the background through a rainbow using `ctx.time`; its only
job is to prove the loop drives a scene. Step 8 swaps in the real demo.

### One small platform edit

We also taught `pump_events()` to quit on **ESC**, a convenience kill-switch. Real,
normalized keyboard input (with pressed/released/held distinctions) arrives in
Chapter 06; this is just so you can close the window from the keyboard today.

---

## 6. Run & observe

```sh
cmake --build build
./build/demo
```

The window now **cycles colors smoothly** instead of sitting on one. ESC or the
close button exits. Even if your display refreshes at 60, 120, or an uneven rate,
the color advances at a consistent pace — because it's driven by simulated time
accumulated in fixed `1/60 s` steps, not by raw frame timing.

```sh
HAND_ENGINE_FRAMES=120 ./build/demo   # ~2 s of animation, exits 0
```

---

## 7. Common pitfalls

- **Using `dt` inside `update`.** Logic should use the fixed step (`kFixedDt`), not
  the variable render `dt` — mixing them brings back non-determinism.
- **Drawing inside `update`.** `update` advances state; only `render` touches the
  framebuffer. Drawing in `update` would draw 0–N times per frame.
- **No clamp.** Without the `0.25` cap, a single long stall can freeze the game in
  a catch-up spiral.
- **Tying animation speed to frame rate.** Animate from `ctx.time` (or per-step in
  `update`), never "per frame", or it runs faster on faster machines.

---

## 8. Glossary

- **Frame** — one trip through read→update→render.
- **Variable timestep** — advancing by real `dt`; simple but non-deterministic.
- **Fixed timestep** — advancing logic in equal `1/60 s` chunks.
- **Accumulator** — stored unspent real time, drained in fixed chunks.
- **Spiral of death** — runaway catch-up after a long frame; prevented by clamping.
- **Interpolation / `alpha`** — blending between fixed steps for smooth visuals.
- **Inversion of control** — the browser calls your frame function, not vice-versa.
- **RAII** — resources freed automatically when their owner is destroyed.

---

## 9. Exercises

1. **Slow the cycle.** Change `ctx.time * 0.7` to `ctx.time * 0.2` in
   `ColorScene::render`. Why does using `ctx.time` (not per-frame `dt`) make speed
   independent of frame rate? *(Hint: `time` is the same total regardless of how
   many frames it took to get there.)*
2. **Count updates.** Put `static int n = 0; ++n;` inside `App`'s `while` loop and
   print `n` once per simulated second. Confirm it climbs ~60/s no matter how fast
   rendering runs. *(Hint: print when `time_` crosses each integer.)*
3. **Trace on paper.** With `FIXED_DT = 1/60`, continue the §3 table for two more
   frames with `dt = 0.005` then `dt = 0.040`. How many updates run each frame, and
   what's the accumulator afterward?
4. **Reason about the clamp.** A frame takes 2.0 s. How many `update` calls happen
   *with* the `0.25` clamp vs *without* it? *(Hint: updates ≈ dt / FIXED_DT.)*

---

## 10. What's next

We have a window and a beating heart, but we can only fill the screen with flat
color. Before we draw anything structured we need **math** — vectors and matrices —
which underpins both 2D drawing now and the real 3D core at M3. **Chapter 04**
builds the math library from scratch and tests it with `ctest`.
