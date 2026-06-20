# Chapter 16 — FPS: Raycasting

> **Goal of this chapter.** Build a first-person, pseudo-3D view from a 2D grid by
> casting **one ray per screen column** (the Wolfenstein technique). It teaches
> projection cheaply — the stepping stone to the real 3D core at M3 — and reuses
> the M0 engine: it's just an `engine::Scene` drawing vertical strips with
> `renderer2d`.

---

## 1. The idea

A raycaster fakes 3D without triangles. The world is a 2D grid of walls. For each
vertical column of the screen we shoot a ray from the player out into the grid,
find the first wall it hits, and draw a vertical strip whose **height is inversely
proportional to the distance** — near walls are tall, far walls short. 640 columns
= 640 rays, cheap enough to do in software at 60 FPS.

```
   top-down (rays fan out)              what each ray draws (one column)
        \  |  /                          near wall:  |#####|   (tall strip)
         \ | /                           far wall:   |  #  |   (short strip)
          \|/      one ray per
   player  *  ---> screen column
```

## 2. The map and the camera

The map (`fps/map`) is a grid: `0` = empty, `>0` = a wall id (picks the texture).
Out-of-bounds counts as wall so nothing escapes.

The camera uses the elegant **direction + plane** formulation (Lodev's): a position
`pos`, a facing vector `dir`, and a `plane` vector perpendicular to `dir` whose
length sets the field of view (`|plane| = tan(FOV/2) ≈ 0.66` → ~66°). For column
`x`, `cameraX = 2x/W − 1` runs −1…+1 across the screen, and the ray direction is
`dir + plane·cameraX`. Turning is just rotating both `dir` and `plane`; strafing
moves along `plane`, walking along `dir` — with **grid collision** (don't step into
a wall cell).

## 3. DDA: stepping to the wall

We find the wall with a **Digital Differential Analyzer**: from the player's cell,
repeatedly jump to the next vertical *or* horizontal grid line (whichever is
nearer) until the cell is a wall. It's integer-grid stepping — no marching in tiny
increments, no misses. (`cast_ray` in `fps/raycast.cpp`.)

The one subtlety that makes or breaks it: use the **perpendicular distance** to the
wall, not the ray's true length. The true length would make walls bulge outward at
the screen edges (**fisheye**); the perpendicular distance (distance projected onto
`dir`) keeps flat walls flat. Our unit test `test_no_fisheye` asserts two symmetric
rays hit a flat wall at the *same* perpendicular distance.

> Because the cast is the heart of the renderer, we kept it a **pure function**
> (`cast_ray`) and test it like chess's perft: known position + direction → exact
> distance, wall id, side, and fractional hit point. `tests/test_fps.cpp` covers
> the start position's east/south casts and the no-fisheye property.

## 4. Drawing a wall strip — with texture

Strip height = `screenH / perpDist`; centre it vertically. For a **textured** wall
we also need *which* texture column to sample: from the exact hit point we take the
fractional `wall_x ∈ [0,1)`, scale to the texture width, and (with small
orientation flips per side/direction) sample straight down the strip, stepping the
texture-y coordinate by `texH / stripHeight` per screen pixel. A little
**distance fog** (darken with distance) plus darker shading on y-facing walls gives
a strong sense of depth. Textures are generated procedurally (`fps/textures` —
stone/brick/wood, deterministic, no assets) but reuse `gfx::Image`, so the sampler
treats them exactly like any loaded image.

```cpp
const Hit h = cast_ray(map_, posX_, posY_, rayDirX, rayDirY);
int lineH = H / h.perp_dist;                 // taller when closer
int texX  = int(h.wall_x * texW);            // which texture column
double step = texH / (double)lineH, texPos = (start - rawStart)*step;
for (y = start..end) { color = tex[ (int)texPos ][texX] * shade; texPos += step; }
```

## 5. Run & observe

```sh
cmake --build build
./build/demo --fps          # WASD move, A/D strafe, arrows turn, ESC quit
```

You walk a textured maze: stone border, a brick room with a doorway, wood pillars.
Walls grow as you approach; flat walls stay flat (no fisheye); distance fades to
dark. `HAND_ENGINE_FRAMES=60 ./build/demo --fps` runs head-less and exits 0.

## 6. Common pitfalls

- **Fisheye** — using ray length instead of perpendicular distance.
- **Mailbox index wrap** — step by cell with `Map::at` bounds, never raw arithmetic.
- **Texture orientation** — flip `texX` by side/direction or textures mirror.
- **No collision** — clamp movement against wall cells (axis-separated).

## 7. Glossary

- **Raycasting** — per-column ray→wall projection (2.5D).
- **DDA** — integer grid-stepping ray traversal.
- **Camera plane** — the vector that, with `dir`, spans the view frustum (sets FOV).
- **Perpendicular distance** — distance along `dir`; removes fisheye.
- **`wall_x`** — fractional hit position along a wall, for texturing.

## 8. Exercises

1. **Change the FOV** by scaling `plane` (try 0.4 and 1.0). What happens to the view?
2. **Add a wall type** to the map + a texture, and walk up to it.
3. **Mini-map**: draw the grid + player in a corner with `fill_rect`/`draw_line`.
4. **Mouse-look**: rotate `dir`/`plane` from `input.mouse_x` deltas.

## 9. What's next

**Chapter 17** populates the world with **billboard sprites** (occluded correctly
via a per-column depth buffer) and gives the engine its **first real audio**.
