# 86 · Sprite Animation — the Flipbook

> Code: `src/engine/anim/flipbook.{hpp,cpp}` (lib `tween_core`) · test `tests/test_flipbook.cpp`
> Asset: `assets/sprites/spin_8.hrt` · Seen live: `./build/demo --anim`

## Why this chapter exists

Textured sprites (ch.77) put a *picture* on an actor. Animation makes that picture
*move* — a walk cycle, a spinning coin, a flickering torch. The classic 2D way is a
**sprite sheet**: many frames packed into one image, shown in sequence. This chapter
adds the tiny piece of state that drives that sequence — a `Flipbook` — and it's the
"animation" item from the Track-A vision, sitting right next to the `Tween` (ch.83)
in the same `anim` library.

## The deferral, and the one idea that lifted it

This slice was deliberately *not* built the first time it came up. A `Flipbook`
with no multi-frame asset to play is a core with no consumer — exactly the kind of
speculative code this project avoids (YAGNI). The blocker was: how do you draw one
frame out of a packed sheet without a new renderer primitive?

The answer is a layout choice. Pack the sheet **vertically** — frame 0 on top, frame
1 below it, and so on. Now frame *i* occupies rows `[i·fh, (i+1)·fh)`, which in a
row-major image is a **single contiguous run of pixels**. So a frame is just a
`gfx::Sprite` that points at the right offset:

```cpp
gfx::Sprite AnimScene::frame_sprite(int f) const {
    return gfx::Sprite{sheet_.pixels.data() + std::size_t(f) * fh_ * sheet_.w, sheet_.w, fh_};
}
```

No sub-rectangle blit, no new renderer code — the existing `blit_scaled` draws that
view directly. (A *horizontal* sheet would break this: each frame's rows are
interleaved with its neighbours', so a frame isn't contiguous and you'd need a
source-rect blit.) One layout decision turned a renderer change into a pointer
offset. That's the whole reason the slice is small.

## The Flipbook

The core knows nothing about pixels. It answers one question — *which frame now?* —
given how much time has passed:

```cpp
struct Flipbook {
    int   frames = 1;
    float fps    = 8;
    bool  loop   = true;
    float t      = 0;
    void update(float dt);
    int  frame() const;
    bool done()  const;
    void reset();
};
```

`frame()` is `int(t · fps)`, wrapped or clamped:

```cpp
int Flipbook::frame() const {
    if (frames <= 1) return 0;
    int f = fps > 0 ? int(t * fps) : 0;
    if (loop) { f %= frames; if (f < 0) f += frames; return f; }   // wrap
    return f < 0 ? 0 : (f >= frames ? frames - 1 : f);             // clamp + hold
}
```

The subtle part is `update`. A naive `t += dt` grows forever, and after a few
minutes `t · fps` is a large float where integer steps lose precision and the
animation starts to *stutter*. So when looping we keep `t` inside a single period:

```cpp
void Flipbook::update(float dt) {
    t += dt;
    if (loop && fps > 0 && frames > 0) {
        const float period = float(frames) / fps;        // seconds for one cycle
        if (period > 0) while (t >= period) t -= period;  // bounded → no drift
    }
}
```

The test pins this directly: after a thousand `0.03 s` steps it asserts both that
every `frame()` stayed in `[0, frames)` **and** that `t` never exceeded one period.
Like the Tween, it's pure and deterministic — no clock, fed `dt` — so `test_flipbook`
runs headless with no window.

## The asset

`assets/sprites/spin_8.hrt` is a 48×384 vertical sheet: eight 48×48 frames of a
loading-spinner — eight dots on a ring, a bright "head" that steps around with a
fading trail. It's generated offline with `encode_hrt` (the same way the wall and
studio textures are made), so the runtime still only ever *reads* `.hrt` bytes and
hand-decodes them — no image library, per the thin-shim rule.

## Seeing it: `--anim`

The scene loads the sheet and plays it: the big current frame upscaled with crisp
nearest-neighbour pixels, the full strip below it with the active frame outlined in
green, and `fps` / `loop` / `playing` / `restart` controls plus a frame counter.
Drag `fps` and the spinner speeds up; untick `loop` and it plays once and **holds**
on the last frame (the counter shows "(done)"); hit `restart` to replay. The sliders
write straight into the `Flipbook` each `update`, so the core and the UI never drift
apart.

## What was deliberately left out

The obvious next step is **sandbox animated actors**: put `frames`/`fps` on the
sandbox `Sprite`/`Archetype`, serialize them, and let an authored actor animate its
textured sheet — the point where this Track-A primitive meets the Track-B content
tools. It's left as a follow-up so this slice stays about the primitive and its
demo. Also deferred: per-actor phase offset (so a crowd doesn't animate in lockstep),
ping-pong frame order, a callback on the last frame, and deriving the frame count
from the asset instead of a hand-set field.

## Try it

```sh
ctest --test-dir build -R '^flipbook$' --output-on-failure   # the headless proof
./build/demo --anim                                          # play the spinner, drag fps
```
