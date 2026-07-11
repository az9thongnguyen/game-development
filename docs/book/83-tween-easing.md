# 83 · Tween & Easing — the interpolation primitive

> Code: `src/engine/anim/tween.{hpp,cpp}` · lib `tween_core` · test `tests/test_tween.cpp`
> Seen live: `./build/demo --fx` → tick the **sweep** box.

## Why this chapter exists

Everything that *moves smoothly* — a camera gliding to a target, a menu panel
sliding in, a health bar draining, a particle emitter drifting — is the same
computation underneath: **advance a value from A to B over a duration, shaped by
a curve.** Up to now the codebase hand-rolled that each time (the particle fade
in ch.79 lerps colour linearly by hand). This chapter gives the engine one small,
pure primitive so nothing has to hand-roll it again. It is the floor the future
animation features (sprite flipbooks, camera rigs, UI transitions) stand on.

It slots into Track A ("the engine cần support tốt hơn, không chỉ 2d/3d"): a
capability *below* rendering that any subsystem consumes.

## Two pieces: a curve catalog and a Tween

### `ease(Ease e, float t)`

`Ease` is an enum of thirteen named curves — `Linear`, `SmoothStep`, the
`Quad`/`Cubic` in/out/inout family, `SineInOut`, `ExpoOut`, `BackOut`,
`ElasticOut`, `BounceOut`. `ease()` maps a progress `t` to an *eased* progress.

Three design decisions matter:

1. **`t` is clamped to `[0,1]` on entry.** A caller can pass a raw, possibly
   out-of-range progress (say `elapsed/dur` that overshot) and never has to guard
   — the curve pins its ends.
2. **Every curve satisfies `f(0)=0` and `f(1)=1`.** That is the contract the
   test enforces for all thirteen. It means you can swap any easing for any other
   without the animation starting or ending in a different place.
3. **`BackOut` and `ElasticOut` deliberately return values *outside* `[0,1]`
   in the middle.** That overshoot *is* the effect — the "anticipation" bounce
   past the target and settle back. Only the endpoints are pinned, not the path.

The curves are the standard Penner forms. `BounceOut` is the familiar
four-segment parabola:

```cpp
float bounce_out(float t) {
    const float n1 = 7.5625f, d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) { t -= 1.5f  / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    /* else */         { t -= 2.625f/ d1; return n1 * t * t + 0.984375f; }
}
```

Alongside sits `lerp(a, b, t)` — plain linear interpolation, *no* clamp, meant to
be fed an already-eased `t`. Keeping the clamp in `ease()` and out of `lerp()` is
deliberate: you clamp once, at the curve, then interpolate freely.

### `struct Tween`

A `Tween` is the stateful driver. It is a plain aggregate — `from`, `to`, `dur`,
an `Ease`, and a `pingpong` flag — plus two runtime fields (`elapsed`,
`reversing`). You own one and pump it:

```cpp
anim::Tween t{100, 200, 1.0f, anim::Ease::CubicOut};
t.update(dt);            // advance
float x = t.value();     // eased current value
if (t.done()) { ... }    // one-shot finished
```

- **`value()`** computes progress `elapsed/dur`, clamps it, mirrors it when
  `reversing`, and returns `lerp(from, to, ease(ease_fn, progress))`.
- **`update(dt)`** advances `elapsed`. One-shot tweens **saturate** at `dur` (the
  value holds at `to` forever). Ping-pong tweens **bounce**: each full `dur`
  consumed flips `reversing`, and a `while` loop drains multiple legs from a
  single big `dt` so there is **no drift** — feed it `5*dur` at once and it lands
  exactly where stepping `dur` five times would.
- **`done()`** is true only for a finished one-shot; a ping-pong never finishes.

## Determinism

There is no RNG and no wall clock anywhere in this file — `value()` is a pure
function of the fields, and `update()` only adds `dt`. So the same `dt` stream
reproduces the same values bit-for-bit. The test asserts this directly (two
identical tweens fed the same 30-step stream stay `==`), which is exactly the
property the fixed-timestep `update()` loop (ch. on `App`) needs: logic that
animates stays replay-safe and headless-testable.

That is why `test_tween` needs no SDL and no window — it is the same "pure core,
scene-side glue" split the particle system uses.

## Seeing it: the `--fx` sweep

The particle playground gains a **sweep** checkbox. When on, a ping-pong
`SineInOut` tween drives the fountain's emitter X back and forth across the
screen:

```cpp
sweep_ = anim::Tween{0.0f, 1.0f, 2.4f, anim::Ease::SineInOut, /*pingpong=*/true};
...
const float ex = sweep_on_ ? 80.0f + sweep_.value() * float(w_ - 160)
                           : float(w_) * 0.5f;
sys_.update(dt, ex, float(h_) - 30.0f, fountain_);
```

The `SineInOut` curve is what makes the sweep read as *motion* rather than a
slide on rails — it eases out at each edge and accelerates through the middle,
the way a pendulum does. Swap `Ease::SineInOut` for `Ease::Linear` and the same
sweep instantly looks mechanical. That one-line difference is the whole reason
easing curves exist.

## What was deliberately left out

- **No `Tween<T>` template.** Scalar `float` is all any current caller needs;
  a colour or a 2-vector is just two or three scalar tweens. A generic vector
  tween goes in only when a real caller wants one. `// ponytail: scalar covers it`
- **No timeline / sequencer / manager.** A caller owns its tween and pumps it.
  Chaining ("move, then fade, then wait") is a later slice if it's ever asked for.
- **No completion callbacks.** `done()` is polled, which keeps all control flow
  visible in the caller's `update()` — no hidden jumps out of an animation.

## Try it

```sh
ctest --test-dir build -R '^tween$' --output-on-failure   # the headless proof
./build/demo --fx                                         # tick "sweep" and watch the curve
```
