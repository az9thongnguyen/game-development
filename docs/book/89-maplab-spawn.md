# 89 · Author-Placed Spawn — the level owns the player start

> Code: `src/games/fps/map.{hpp,cpp}` (the `fpsmap1` format), `maplab_scene.cpp`, `raycast_scene.cpp`
> Test: `tests/test_fps.cpp` (`test_map_serialize`) · Asset: `assets/maps/level_00.map`
> Seen live: `./build/demo --maplab` → **Tool: Spawn** → click a cell → Save; then `./build/demo --fps`

## Why this chapter exists

The Map/Level Lab (ch.78) let you author a level's *walls* — but not where the
player starts. The raycaster's spawn was a hard-coded `posX_(3.5), posY_(8.5)` in
its constructor, so every authored level dropped you in the same spot regardless of
its layout. Paint a level whose (3.5, 8.5) sits inside a wall and you'd start
*embedded in it*. This chapter closes that gap: the **level file** now carries its
own player start, the Lab places it, and the raycaster reads it. It's a small slice,
but it removes a real trap — the kind of hard-coded assumption that silently breaks
the moment content stops matching the code's guess.

## The format grows one optional line

`fpsmap1` was: a header, a `size`, then `h` rows of cell ids. The spawn is one more
optional line at the end:

```
fpsmap1
size 16 16
row 1 1 1 ...
...
spawn 3 8 0.000000        <- cell (3,8), facing 0 rad (+x)
```

The `Map` struct gains three fields with an "unset" sentinel:

```cpp
int   spawn_cx = -1;   // < 0 means "no spawn authored"
int   spawn_cy = -1;
float spawn_dir = 0.0f;
```

The key property is **backward compatibility through omission**. `to_text` writes the
`spawn` line *only* when one is set (`spawn_cx >= 0`); `from_text` reads a trailing
`spawn` line *if present* and validates it against the grid bounds, leaving the
sentinel untouched otherwise:

```cpp
if (in >> tok && tok == "spawn") {
    int cx, cy; float dir;
    if ((in >> cx >> cy >> dir) && cx >= 0 && cy >= 0 && cx < m.w && cy < m.h) {
        m.spawn_cx = cx; m.spawn_cy = cy; m.spawn_dir = dir;
    }
}
```

So every `.map` file written before this chapter still parses perfectly — it just
reports "no spawn", and the raycaster falls back to its old default. The test pins
all three cases: a spawn-less map round-trips with `spawn_cx == -1`, a map *with* a
spawn round-trips its `spawn 2 1` token exactly, and an out-of-bounds `spawn 9 9`
is rejected while the grid itself still loads. Fail-closed, like the rest of the
format.

## The raycaster reads it (without disturbing the default)

The constructor still initialises the default start in its member-init list — that's
the fallback and it stays. Reading the authored spawn is a few lines in the *body*,
guarded by the sentinel:

```cpp
if (map_.spawn_cx >= 0 && map_.spawn_cy >= 0) {
    posX_ = map_.spawn_cx + 0.5;                       // cell centre
    posY_ = map_.spawn_cy + 0.5;
    dirX_ = std::cos(map_.spawn_dir); dirY_ = std::sin(map_.spawn_dir);
    planeX_ = -dirY_ * 0.66; planeY_ = dirX_ * 0.66;   // camera plane ⟂ dir, FOV 0.66
}
```

The one subtlety is the **camera plane**. In a raycaster the plane vector is
perpendicular to the view direction and its length sets the field of view (0.66 here
≈ 66°). For the default `dir = (1,0)` the plane is `(0, 0.66)` — exactly the old
hard-coded value, which is why an unset spawn behaves identically to before. For any
other facing, rotating `dir` by θ means the plane is `(-sin θ, cos θ)·0.66`. Get that
perpendicular wrong and the world shears or mirrors; getting it right is what makes an
authored facing actually *look* the right way.

## Placing it in the Lab

The Lab's single Paint/Fill toggle became a three-way **Tool** cycle —
Paint → Fill → **Spawn**. In Spawn mode a click sets the start cell, and a
**Facing** button cycles E → S → W → N (the map's +y is downward, so those map to
`0, π/2, π, 3π/2`). A green dot with a facing tick marks the spawn on the grid so you
can see exactly where — and which way — the player will appear:

```cpp
else if (tool_ == 2) {   // Spawn
    if (in.pressed) { map_.spawn_cx = hx; map_.spawn_cy = hy; map_.spawn_dir = facing_ * kHalfPi; }
}
```

Save writes the spawn into the `.map` through the same `to_text` path, so what you
place is what `--fps` loads.

## Self-contained levels

`assets/maps/level_00.map` was re-seeded through the real codec to carry
`spawn 3 8 0` — the same spot the raycaster used to hard-code, but now *owned by the
file* rather than assumed by the code. The level is self-describing: hand it to the
raycaster and it knows where to put you, no out-of-band knowledge required. That's
the whole point of the slice — the content stopped depending on a constant buried in
a constructor.

## What was deliberately left out

- **Sub-cell / free-angle spawns** — the Lab snaps to a cell and four facings; the
  format already stores `spawn_dir` as a float, so finer control is a UI change only.
- **Multiple spawns / spawn *types*** (player vs. enemy vs. item) — the sandbox has
  richer placement; the raycaster only needs one player start today.

## Try it

```sh
ctest --test-dir build -R '^fps$' --output-on-failure   # includes the spawn round-trip
./build/demo --maplab     # Tool: Spawn → click → Facing → Save
./build/demo --fps        # starts where the level says
```
