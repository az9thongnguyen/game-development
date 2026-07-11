# Tween & Easing — design

**Track A (engine depth). Date: 2026-07-11.**

## Goal

Give the engine a small, pure **animation-interpolation** core: a catalog of
named easing curves plus a deterministic scalar `Tween`. This is the foundation
every higher animation feature (sprite flipbooks, camera moves, UI transitions,
particle parameter sweeps) is built on, and it fills the "engine cần support tốt
hơn, không chỉ 2d/3d" gap directly below rendering.

## Why now

The particle system (ch.79) already hand-rolls a linear `t_of`/`lerp8` fade.
Camera, UI, and future sprite animation all want the same "advance a value along
a curve over time" primitive. One reusable core beats N ad-hoc lerps.

## Non-goals (YAGNI)

- No `Tween<T>` template over arbitrary types — scalar `float` covers 99% of
  uses (positions/colours decompose into scalars). Add vector tweens only if a
  real caller needs one.
- No tween *manager*/timeline/sequencer. A caller owns its `Tween` and calls
  `update(dt)`. Sequencing is a later slice if asked.
- No callbacks/events on completion — `done()` is polled (matches the fixed-step
  `update` loop; no hidden control flow).

## Design

`namespace anim`, files `src/engine/anim/tween.{hpp,cpp}`, static lib
`tween_core`, headless test `test_tween`.

### Easing catalog

```
enum class Ease {
  Linear, SmoothStep,
  QuadIn, QuadOut, QuadInOut,
  CubicIn, CubicOut, CubicInOut,
  SineInOut, ExpoOut, BackOut, ElasticOut, BounceOut
};
float ease(Ease e, float t);   // t clamped to [0,1]; returns eased progress
```

- Standard Penner-style curves. `BackOut`/`ElasticOut` deliberately overshoot
  outside `[0,1]` (that's the effect); all curves satisfy `f(0)=0`, `f(1)=1`.
- `t` is **clamped to [0,1] on entry** so callers can't feed garbage.
- `float lerp(float a, float b, float t)` helper (no clamp — pairs with eased t).

### Tween

```
struct Tween {
  float from=0, to=1, dur=1;
  Ease  ease_fn = Ease::Linear;
  bool  pingpong = false;      // reverse at each end, run forever
  float elapsed = 0;
  bool  reversing = false;     // internal ping-pong phase
  float value() const;         // lerp(from,to, ease(ease_fn, progress))
  void  update(float dt);      // advance; hold at end (one-shot) or bounce (pingpong)
  bool  done() const;          // one-shot only, true once elapsed>=dur
  void  reset();
};
```

- **Deterministic**: same `dt` sequence → same `value()`. No RNG, no wall clock.
- One-shot: `elapsed` saturates at `dur`, `value()==to`, `done()==true`.
- Ping-pong: each full `dur` flips `reversing`; `value()` mirrors progress; never
  `done()`. Consumes multiple `dur`s per big `dt` via a while-loop (no drift).
- `dur<=0` guarded → progress treated as 1 (instant).

## Integration (visible use)

`--fx` fountain gains a **"sweep"** checkbox: a ping-pong `SineInOut` tween drives
the emitter's X across the screen, demonstrating the core in a live scene.
Off → emitter stays centred (current behaviour).

## Testing

`tests/test_tween.cpp`, dependency-free (`assert`/count, no SDL):
- every `Ease` value: `ease(e,0)≈0`, `ease(e,1)≈1`.
- clamp: `ease(Linear,-0.5)==0`, `ease(Linear,1.5)==1`.
- `SmoothStep(0.5)==0.5`; `lerp` endpoints + midpoint.
- `Tween`: `value()==from` at start, `==to` at/after `dur`, `done()` flips.
- ping-pong: back to `from` after `2*dur`, never `done()`, no drift after 5*dur.
- determinism: two identical tweens, identical `dt` stream → identical values.
