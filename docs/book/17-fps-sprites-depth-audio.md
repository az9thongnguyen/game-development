# Chapter 17 — FPS: Sprites, Depth & Audio

> **Goal of this chapter.** Put things *in* the world — billboard sprites that face
> the camera and are correctly hidden behind walls — and give the engine its first
> **real audio**. Then run the M2 acceptance checks.

---

## 1. The depth buffer

To know whether a sprite pixel is in front of or behind a wall, the wall pass
records, for **each screen column**, the perpendicular distance to the wall it
drew: a 1D **depth buffer** `zbuf_[x]`. A sprite column is drawn only where the
sprite is *nearer* than the wall recorded there (`spriteDepth < zbuf_[x]`). That's
the whole occlusion trick — a per-column version of the z-buffer we'll build for
real triangles at M3.

## 2. Billboard sprites

A billboard is a flat image that always faces the camera. To place one we convert
its world position (relative to the player) into **camera space** with the inverse
of the `[plane dir]` matrix (`project_sprite` in `fps/raycast.cpp`, unit-tested):

```cpp
invDet = 1 / (planeX*dirY − dirX*planeY);
tx = invDet*( dirY*relX − dirX*relY);        // horizontal offset
ty = invDet*(−planeY*relX + planeX*relY);    // depth (>0 = in front)
```

`ty` is the sprite's perpendicular distance (compare against `zbuf_`); `screenX =
W/2·(1 + tx/ty)` is where it lands; its on-screen size is `H/ty` (same projection
as walls). We then draw it column-by-column: skip columns occluded by a nearer
wall, sample the sprite texture, and **skip fully transparent texels** (alpha 0) so
the barrel's silhouette shows. Sprites are sorted **far-to-near** so nearer ones
paint over farther ones.

```cpp
sort sprites by distance (far first);
for each sprite:
    project -> screenX, size; if ty <= 0 skip (behind)
    for stripe in [left,right):
        if ty < zbuf_[stripe]:           // in front of the wall here
            for y in [top,bottom): blit sampled texel (skip alpha==0)
```

The test `test_project_sprite` pins the math: a sprite straight ahead is centred
(`tx==0`) at depth = distance; one to the side gives `tx>0`; one behind gives
`ty<0`.

## 3. Real audio (the engine's first sound)

Audio lives behind the platform seam (Chapter 07's stub becomes real). `init_audio`
opens an SDL audio device — **signed 16-bit, mono, 44.1 kHz** — and `play_sound`
queues a one-shot clip with `SDL_QueueAudio`. If the device can't open (e.g. a
head-less CI box) it degrades to a no-op, so the game still runs. Game code never
touches SDL audio; it just calls `platform::play_sound(samples, count)`.

The sounds themselves are **generated procedurally** (no asset files):

- **Gunshot** — white noise (a tiny LCG) times a fast exponential decay.
- **Footstep** — a short low sine "thump" with decay.

The FPS plays the gunshot on **Space** (latched to one shot per press) and a
footstep on a timer while you're moving.

> Caveat (honest): M2 uses `SDL_QueueAudio`, which *appends* rather than mixes, so
> overlapping clips queue up; we drop new clips if a lot is already queued. A
> proper callback **mixer** is the standard upgrade (a good M2+ exercise).

## 4. M2 acceptance

| Criterion | How verified | Result |
|-----------|--------------|--------|
| Walls render in first person, no fisheye | `test_fps` (perp distance) + run | ✅ |
| Textured walls | run `--fps` | ✅ |
| Billboard sprites, occluded by walls | depth buffer + `project_sprite` test | ✅ |
| Grid collision | can't enter wall cells | ✅ |
| Real audio (shoot/step) | device opens; clips play | ✅ (no-op if no device) |
| Unit tests | `ctest` (math + chess + fps) | ✅ all pass |
| No leaks | `leaks` on a head-less `--fps` run | ✅ |
| No SDL above the platform layer | `grep SDL_ src/games/fps` | ✅ none |
| Warning-clean build | `-Wall -Wextra -Wpedantic` | ✅ |

Reproduce: `ctest --test-dir build --output-on-failure`, then `./build/demo --fps`.

## 5. Common pitfalls

- **No depth test for sprites** → they draw over walls. Always check `zbuf_`.
- **Wrong draw order** → far sprites cover near ones. Sort far-to-near.
- **Ignoring sprite alpha** → opaque boxes instead of shaped sprites.
- **Blocking the loop on audio** → use the queue/no-op seam; never busy-wait.

## 6. Glossary

- **Depth (z) buffer** — nearest distance per column; gates sprite drawing.
- **Billboard** — a camera-facing flat sprite.
- **Camera-space transform** — mapping world-relative position to screen via the
  inverse camera matrix.
- **One-shot / queue vs mixer** — append-and-play vs blend-many audio.

## 7. Exercises

1. **More sprites / a second type** — add a different procedural sprite.
2. **Mixer** — replace `SDL_QueueAudio` with a callback that sums active clips.
3. **Shoot-to-remove** — on Space, raycast forward and delete the nearest sprite hit.
4. **Sprite scaling test** — assert on-screen size doubles when distance halves.

## 8. What's next

M2 is complete: a textured raycaster with occluded sprites and real sound, all on
the M0 engine and merged to `main`. Next is **M3 — the real 3D core**: a software
triangle rasterizer with a true z-buffer, perspective projection, meshes, and
cameras (the math from Chapter 04 finally drives 3D). That's the project's pillar,
and it gets its own spec, plan, and chapters.
