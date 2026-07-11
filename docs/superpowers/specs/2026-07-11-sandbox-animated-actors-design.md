# Sandbox Animated Actors — design

**Track A + B join. Date: 2026-07-11.**

## Goal

Let a sandbox actor carry a **sprite-sheet animation**: pick an animated sheet in
the inspector and the placed actor plays it, in edit and in Play, and the animation
survives save/load. This wires the Flipbook (ch.86) into the content tool (ch.77
textured sprites) — the point where engine-depth meets the mini studio.

## Design (data-first, no new systems)

The sandbox already shares one flat `Archetype` across palette / ECS `Sprite` /
`sandbox1` text. Add two fields there and they travel every existing path:

- `Sprite` + `Archetype`: `int frames = 1; float fps = 8.0f;` (`frames>1` ⇒ the
  texture is a vertical N-frame sheet).
- Spawn funnel copies them; `archetype_tokens` emits `frames=`/`fps=` **only when
  `frames>1`** (static scenes unchanged); `parse_archetype` + `to_scene` read them.

### Drawing

Reuse ch.86's contiguous-rows slice. Frame index from a **scene-wide cosmetic
clock** `double anim_time_` accumulated in `update()` (even when paused):
`f = int(anim_time_ * fps) % frames`. No per-entity state → nothing extra to
serialize, and animation is never part of the deterministic `World::tick`.

### Authoring

`load_textures()` also loads known sheets into `tex_` and records their frame count
in `sheet_frames_` (`spin_8 → 8`). Cycling the inspector **Tex** button to a sheet
auto-sets `s->frames` from that registry (flat texture / none ⇒ `frames=1`); an
`anim fps` slider appears when `frames>1`.

## Testing

`tests/test_sandbox.cpp` (`test_animated_sprite_roundtrip`): an animated actor
spawns with the right frames/fps, emits `frames=`/`fps=` tokens, round-trips
`to_scene→from_scene→to_scene` unchanged; a static actor emits neither token.

## Deferrals (ponytail)

Per-actor phase offset (shared clock = lockstep) / ping-pong order; deriving frame
count from the asset instead of the `sheet_frames_` registry; Studio sheet-export
(the Texture Lab still makes single textures — the next Track-B slice, which would
feed this consumer).
