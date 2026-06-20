# M2 — FPS Raycaster: Design & Implementation Plan

> Status: spec + plan. Built on the M0 engine + the M1-era additions (image
> loader, smooth/HiDPI present, polled input). Branch: `feat/m2-fps`.

## Context & goals (requirements.md §8)

A Wolfenstein-style **raycaster**: a 2D grid map projected to a pseudo-3D first-
person view by casting one ray per screen column. The stepping stone to the *real*
3D core at M3 — it teaches projection cheaply before triangles + z-buffers.

Deliverables: walk a textured maze in first person; billboarded enemy sprites;
grid collision; the engine's **first real audio** (gun/footstep). Real-time, fixed
timestep, 60 FPS.

## Decisions (defaults; adjustable)

- **Algorithm:** the classic **DDA raycaster** in "dir + camera-plane" form
  (Lodev's method) — robust, integer-grid stepping, no fisheye (perp distance).
- **Resolution:** framebuffer **640×400, scale 1, smooth + HiDPI** (crisp, not the
  retro 480×270). 640 rays/frame is cheap in software.
- **Player model:** `pos(x,y)`, `dir(x,y)`, `plane(x,y)` (perpendicular to dir,
  |plane| = tan(FOV/2) ≈ 0.66 → ~66° FOV).
- **Controls:** W/S forward-back, A/D strafe, ←/→ rotate (mouse-look optional
  later). ESC quit.
- **Textures:** reuse the **`.hrt` image loader** (engine/image) + the offline
  pipeline; vendor a few wall textures as `.hrt`. (Procedural fallback if needed.)
- **Engine reuse:** raycaster is an `engine::Scene` drawing vertical strips with
  `renderer2d` (fill_rect / set_pixel). FPS *logic* (map, player, raycast) is its
  own module `src/games/fps/`, UI-agnostic where practical.

## Architecture

```
   [ fps: RaycastScene (engine::Scene) ]  -- update/render -->  renderer2d, input
                 |
   [ fps core: Map, player state, DDA raycast, (sprites, textures) ]
                 |
            [ M0 engine + image loader + audio seam ]
```

## Build order (each step = code + guidebook chapter + checkpoint)

1. **Raycaster core.** `fps/map` (grid level), `fps/raycast_scene` (DDA, solid
   walls with N-S/E-W side shading, flat ceiling/floor), movement + **grid
   collision**, `main --fps`. *Checkpoint:* walk around a maze in first person.
   *Chapter.* ← REVIEW PAUSE.
2. **Textured walls.** Per-column texture sampling: from the wall hit compute
   `wallX`, map to a texture column, sample down the strip. Wall textures via
   `.hrt`. *Checkpoint:* textured maze.
3. **Sprites + depth.** A **1D depth buffer** (perpWallDist per column); billboard
   enemy sprites, sort by distance, draw back-to-front, clip per column against the
   depth buffer. *Checkpoint:* enemies stand in the world, correctly occluded.
4. **Real audio.** Implement `platform` audio for real (SDL_OpenAudioDevice +
   callback mixing) behind the existing audio seam; play a gunshot / footstep.
   *Checkpoint:* sound on shoot/step. (First real audio in the engine.)
5. **Acceptance + merge.** Walk a textured maze with sprites + audio, no crash,
   ~60 FPS; chapters written; merge `feat/m2-fps` → `main`.

## Files (new)

```
src/games/fps/
  map.hpp / map.cpp            # grid level + lookup + default level
  raycast_scene.hpp / .cpp     # engine::Scene: DDA render + movement + collision
  (textures.* / sprites.* as steps 2-3 add them)
src/main.cpp                   # add --fps dispatch
assets/fps/                    # wall + sprite .hrt (steps 2-3)
docs/book/                     # M2 chapters (continue numbering after 15)
```

## Testing / verification
- DDA correctness: rays never escape the grid (border walls); perp distance
  removes fisheye (straight walls look straight, not bulged).
- Collision: player cannot enter wall cells (axis-separated check).
- Headless `HAND_ENGINE_FRAMES` run exits 0; interactive walk is smooth at 60 FPS.
- Arch: fps core uses engine/image + renderer via Scene; no SDL above platform.

## Out of scope for M2
- True 3D (triangles, z-buffer, meshes) — that's M3.
- Floor/ceiling texturing, doors, enemy AI/combat depth, level editor.
- Networking. Keep it a tight, correct raycaster that teaches projection.
