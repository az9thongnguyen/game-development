# 87 · Sandbox Animated Actors — where the engine meets the studio

> Code: `src/games/sandbox/{world.hpp,world.cpp,serialize.cpp,sandbox_scene.cpp}`
> Test: `tests/test_sandbox.cpp` (`test_animated_sprite_roundtrip`)
> Seen live: `./build/demo --sandbox` → select an actor → cycle **Tex** to `spin_8`

## Why this chapter exists

The last five chapters each added a *capability* to the engine in isolation:
textured sprites (ch.77), the Tween (ch.83), lighting (ch.84), audio (ch.85), the
Flipbook (ch.86). This chapter is different — it doesn't add a capability, it
**joins two tracks**. It takes the Flipbook (Track A, engine depth) and wires it
into the sandbox (Track B, the content authoring tool) so that an actor *you place
and save* can carry a sprite-sheet animation. This is the moment the "engine" and
the "mini studio" stop being separate demos and become one pipeline: author an
animated actor, save the scene, load it back, and it's still animating.

## The change is almost entirely data

Animation didn't need new systems — it needed two fields on the sprite and for
them to travel everywhere the sprite already travels. The sandbox's discipline
(one flat `Archetype` that the palette, the ECS `Sprite`, and the text format all
share) is what made this a *small* change: add the fields once to the shared shape,
and every path that already round-trips a sprite now round-trips its animation too.

```cpp
struct Sprite {                       // the ECS component
    gfx::Color color; bool round; std::string texture;
    int frames = 1; float fps = 8.0f;      // >1 = the texture is a vertical N-frame sheet
};
struct Archetype { ... std::string texture; int frames = 1; float fps = 8.0f; };
```

The spawn funnel copies them (`{a.color, a.round, a.texture, a.frames, a.fps}`), and
the `sandbox1` text codec emits them **only when the sprite is actually animated**,
so static scenes stay byte-for-byte identical to before:

```cpp
if (a.frames > 1) s += " frames=" + std::to_string(a.frames) + " fps=" + fmt_f(a.fps);
```

`test_animated_sprite_roundtrip` locks this down: an actor with `frames=8, fps=12`
emits both tokens, survives `to_scene → from_scene → to_scene` unchanged, and a
static actor emits neither token.

## Drawing a frame: the shared clock

The sandbox draws every actor's sprite each render. For an animated one it slices
the current frame out of the vertical sheet — the same contiguous-rows trick from
ch.86 — and picks *which* frame from a single scene-wide clock:

```cpp
if (s.frames > 1 && s.fps > 0 && img.h >= s.frames) {
    const int fh = img.h / s.frames;
    const int f  = int(anim_time_ * s.fps) % s.frames;   // shared-clock flipbook
    spr = {img.pixels.data() + std::size_t(f) * fh * img.w, img.w, fh};
}
```

Two deliberate choices here:

- **A shared `anim_time_` clock, not a per-entity `Flipbook`.** Animation in the
  sandbox is *cosmetic* — it isn't part of the deterministic `World::tick`
  simulation state, so it must never be saved or affect replay. A single
  `double anim_time_` accumulated in `update()` keeps the frame index a pure
  function of time with **zero per-entity state and nothing to serialize**. The
  cost is that two actors on the same sheet animate in lockstep. That's the
  `// ponytail:` ceiling; a per-actor phase offset is the upgrade when a scene
  needs a crowd that doesn't march in step.
- **It runs even when the sim is paused.** `anim_time_` advances in `update()`
  regardless of `playing_`, so while you're *editing* (sim stopped) the animation
  still plays — you can see what you're placing. Using `double` means the clock
  stays precise for days, sidestepping the float-drift the Flipbook's `update`
  guards against.

## Authoring it: one click sets the frame count

The friction with sprite sheets is that the frame count lives in the artist's head,
not the pixels. The sandbox removes the guesswork: it loads known sheets into a
small registry (`sheet_frames_["spin_8"] = 8`) alongside the flat textures, and
cycling the inspector's **Tex** button to a sheet *auto-sets* the sprite's frame
count:

```cpp
auto sf = sheet_frames_.find(s->texture);
s->frames = sf != sheet_frames_.end() ? sf->second : 1;   // sheet animates; flat is static
if (s->frames > 1) ui_.slider("anim fps", s->fps, 1.0f, 24.0f);
```

Pick `spin_8` and it animates at 8 frames immediately, with an fps slider appearing
to tune it; pick a flat `studio_NN` texture (or none) and it's static again. No
number to memorize, no separate "is this animated?" toggle — the asset choice
carries the answer.

## What was deliberately left out

- **Per-actor phase offset / ping-pong order** — the shared clock means lockstep;
  fine for now (YAGNI), noted as the upgrade path.
- **Deriving frame count from the asset itself** (e.g. a sidecar or filename
  convention) instead of the `sheet_frames_` registry — worth doing once there are
  many sheets; two entries don't justify the machinery yet.
- **Studio-authored sheets** — the Texture Lab still makes single textures; a
  sheet-export mode (draw/generate N frames) is the natural next Track-B slice, and
  it would feed straight into this consumer.

## Try it

```sh
ctest --test-dir build -R '^sandbox$' --output-on-failure   # includes the anim round-trip
./build/demo --sandbox        # place an actor, select it, cycle Tex to spin_8, hit Play
```
