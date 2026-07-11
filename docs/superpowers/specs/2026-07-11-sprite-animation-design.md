# Sprite Animation (Flipbook) — design

**Track A (engine depth). Date: 2026-07-11.**

## Goal

Play **sprite-sheet animation**: advance a frame index through a sheet at a fixed
rate. This is the "animation" item from the Track-A vision, and it's the payoff of
two earlier slices — it composes with textured sprites (ch.77) and sits beside the
Tween (ch.83) in the same `anim` lib.

## Why it was deferred, and what unblocks it

It was held back once because a flipbook with no multi-frame asset is a core with
no consumer (YAGNI). The unlock is a layout choice: **pack the sheet vertically**.
Then frame *i* is a *contiguous* run of rows in the image, so a plain `gfx::Sprite`
view at the right offset + the existing `blit_scaled` draws it — **no new renderer
primitive, no sub-rect blit**. That removes the friction and keeps the slice lean.

## Design

### Pure core — `engine/anim/flipbook.{hpp,cpp}` (added to `tween_core`)

```
struct Flipbook {
  int   frames = 1;
  float fps    = 8;
  bool  loop   = true;
  float t      = 0;
  void update(float dt);   // loop keeps t within one period (no float drift)
  int  frame() const;      // always in [0, frames-1]
  bool done()  const;      // one-shot only
  void reset();
};
```

- Deterministic (no clock; fed `dt`). `frame()` = `int(t*fps)`, looped via `%` or
  clamped-and-held one-shot. `update` wraps `t` to one period when looping so it
  never grows unbounded. Headless-testable, like Tween.

### The sheet asset

A vertically-packed `.hrt`: `assets/sprites/spin_8.hrt`, 48×384 = 8 frames of
48×48 (a rotating "spinner" with a fading trail), generated headlessly with
`encode_hrt` — the same offline-asset pattern as the wall/studio textures.

### Scene — `--anim` (`games/anim/anim_scene.{hpp,cpp}`)

Loads the sheet, plays it with a `Flipbook`, and shows: the big current frame
(nearest-neighbour upscale), the whole strip with the active frame outlined, and
`fps` / `loop` / `playing` / `restart` controls + a frame counter. `frame_sprite(f)`
returns a `gfx::Sprite` pointing at `pixels + f*fh*w` — the zero-copy contiguous
view. Graceful "asset missing" text if the sheet fails to load.

## Testing

`tests/test_flipbook.cpp`, dependency-free:
- single-frame → always 0; 8@8fps steps one frame per 0.125 s and wraps.
- `frame()` stays in range over a long run; `t` stays bounded to one period.
- one-shot advances, holds on the last frame, `done()` flips; `reset()` rewinds.

## Deferrals (ponytail)

**Sandbox animated actors** — wiring `frames`/`fps` onto the sandbox `Sprite`/
`Archetype` (+ serialize + inspector) so authored actors animate — is the natural
Track-A+B follow-up, left out here to keep the slice focused on the primitive and
its demo. Also: per-actor phase offset, ping-pong frame order, event-on-last-frame,
deriving frame count from the asset instead of a manual field.
