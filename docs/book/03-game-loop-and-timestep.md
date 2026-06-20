# Chapter 03 — The Game Loop & Fixed Timestep

> **What you'll learn:** what a game loop really is, why naively using "time since
> last frame" makes games behave differently on different machines, how a
> **fixed-timestep accumulator** fixes that, and the `Scene`/`App` structure that
> every game in this project plugs into. You'll also see *why* the loop lives in
> the platform layer — it's the key to the web port.

---

## 1. The idea

### What a game loop is

Every real-time game is the same loop, forever:

```
  read input  →  advance the world a little  →  draw the world  →  repeat
```

Each trip around is one **frame**. The question is: *how much* should the world
advance each frame?

### The naive answer, and why it's a trap

The obvious idea: measure the real time since the last frame (`dt`) and move
everything by `velocity * dt`. This is called a **variable timestep**. It seems
fine, but it has real problems:

- **Non-determinism.** Floating-point results depend on the exact `dt` values, so
  the same inputs produce *different* outcomes on a 60 Hz vs 144 Hz display, or
  when the frame rate dips. For chess that's harmless; for physics, collisions,
  and replays it's a nightmare.
- **Big hitches break things.** If one frame takes 0.5 s (you dragged the window),
  a fast object can *teleport through* a wall because it moved half a second in
  one step.

### The fix: a fixed timestep with an accumulator

We separate two clocks:

- **Logic** advances in fixed, equal chunks — always `1/60` of a second.
- **Rendering** happens once per frame, as often as the display allows.

We keep an **accumulator** of unspent real time. Each frame we add the real `dt`
to it, then spend it in fixed `1/60 s` chunks:

```
accumulator += dt
while accumulator >= FIXED_DT:        # may run 0, 1, or several times
    update(FIXED_DT)                  # logic always sees the SAME dt
    accumulator -= FIXED_DT
render()                              # draw once
```

Now logic is deterministic (every `update` sees exactly `1/60`), and rendering
still floats with the display. This is the classic pattern from Glenn Fiedler's
"Fix Your Timestep!", and it's the heartbeat of the whole engine.

Two refinements in our code:

- **Clamp huge frames.** If `dt > 0.25 s`, we clamp it. Otherwise after a long
  pause the `while` loop would try to simulate hundreds of steps to "catch up",
  making things *worse* — the so-called **spiral of death**.
- **`alpha` for smoothness (later).** The leftover `accumulator / FIXED_DT` is a
  fraction in `[0,1)` telling us how far we are *between* logic steps. Renderers
  can interpolate with it for buttery motion. We pass it in the `Context`; M0
  doesn't need it yet, but it's there for M3.

### Why the loop lives in the *platform* layer

Here's the subtle, important part. A normal desktop loop is a blocking
`while (!quit) { ... }`. **The browser forbids that.** Under Emscripten you must
hand the browser *one frame function* and let *it* call you (`emscripten_set_main
_loop`) — if you block in a `while(true)`, the page freezes.

So we put the loop *mechanism* in the platform backend (`platform::run`) and have
it call a `frame(dt)` callback. Desktop implements `run` as a `while` loop; the
web backend (M5) will implement the same `run` using `emscripten_set_main_loop` —
and the engine/game code that just provides a `frame` callback **doesn't change at
all**. The fixed-timestep *logic*, meanwhile, lives in the engine (`App::frame`),
because that's game behavior, not platform plumbing.

> This is why you'll never see a `while(true)` in engine or game code in this
> project. That rule isn't style — it's what keeps the web port a drop-in.

---

## 2. The code

### `scene.hpp` — Scene + Context

A `Scene` is one screen (the demo, chess, a menu). It has two methods:

```cpp
virtual void update(double dt);            // fixed-step logic (default: nothing)
virtual void render(const Context& ctx) = 0;  // draw one frame
```

`Context` is everything needed to draw a frame: the `Framebuffer`, the render
`dt`, total `time`, and `alpha`. Passing a small bundle (instead of globals) keeps
scenes self-contained and easy to test.

### `app.hpp` / `app.cpp` — the App

`App` owns the current scene plus the accumulator and total time. Its one job is
`App::frame(dt)`, which is exactly the accumulator pattern above:

```cpp
if (dt > 0.25) dt = 0.25;                 // clamp the spiral of death
accumulator_ += dt;
while (accumulator_ >= kFixedDt) {        // kFixedDt = 1/60
    scene_->update(kFixedDt);
    accumulator_ -= kFixedDt;
    time_        += kFixedDt;
}
Context ctx{ platform::framebuffer(), dt, time_, accumulator_ / kFixedDt };
scene_->render(ctx);
```

`App` reaches the framebuffer through `platform::framebuffer()` — again, the only
dependency is the platform interface.

### `main.cpp` — wiring it together

```cpp
engine::App app(std::make_unique<ColorScene>());
platform::run([&app](double dt) { app.frame(dt); });
```

`platform::run` drives the loop and calls our lambda each frame; the lambda
forwards to `App::frame`, which runs the scene. `ColorScene` is a throwaway that
animates the background through a rainbow using `ctx.time` — its only purpose is
to prove the loop drives a scene. Step 8 replaces it with the real demo.

### One small platform edit

We also taught `pump_events()` to quit on **ESC** (a convenience kill-switch).
Real, normalized keyboard input arrives in Chapter 06; this is just so you can
close the window from the keyboard now.

---

## 3. Run & observe

```sh
cmake --build build
./build/demo
```

The window now **smoothly cycles through colors** instead of sitting on one. ESC
or the close button exits. The motion is driven by simulated time accumulated in
fixed `1/60 s` steps — even though the display might refresh at 60, 120, or an
uneven rate, the color progression advances at a consistent pace.

Head-less check:

```sh
HAND_ENGINE_FRAMES=120 ./build/demo   # ~2 seconds of animation, exits 0
```

---

## 4. Exercises

1. **Slow it down.** In `ColorScene::render`, change `ctx.time * 0.7` to
   `ctx.time * 0.2`. The cycle should slow. Why does using `ctx.time` (not the
   per-frame `dt`) make the speed independent of frame rate?
2. **Count updates.** Add a `static int n = 0; n++;` inside `App`'s `while` loop
   and print `n` once a second. Confirm it climbs at ~60/second regardless of how
   fast rendering happens.
3. **Provoke the clamp.** Reason about what happens to `accumulator_` if a single
   frame takes 2 seconds, with the `0.25` clamp and without it. How many update
   steps would run in each case?

---

## 5. What's next

We have a window and a beating heart. Before we draw anything interesting we need
**math** — vectors and matrices — which underpins both 2D drawing and the 3D core
later. **Chapter 04** builds the math library and tests it.
