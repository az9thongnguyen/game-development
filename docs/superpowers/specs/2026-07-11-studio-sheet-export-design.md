# Studio Sheet Export — design

**Track B (mini studio), closing the animation loop. Date: 2026-07-11.**

## Goal

Let the Texture Lab **export an animated sprite sheet** from any recipe, saved where
`--anim` and `--sandbox` discover it. This makes the animation pipeline self-hosting:
author in the studio, play in the engine, no throwaway script (the only sheet so far,
`spin_8.hrt`, was script-generated).

## Design

### Animation from tileability (no generator change)

Lab textures are seamlessly tileable, so scrolling one wraps with no seam. Frame `f`
= the texture scrolled horizontally by `f/N` of its width; over N frames it loops
perfectly. Pure, deterministic, reuses `generate()` as-is.

`src/games/studio/sheet.{hpp,cpp}` (in `studio_core`):
```
gfx::Image make_sheet(const TextureParams& p, int frames);
```
Vertical stack of N square frames → `size × (N·size)`. `frames<=1` returns the plain
texture.

### Self-describing frame count

Frames are square + vertical, so `N = h/w`. One shared pure helper in the anim lib:
```
int anim::frames_in_sheet(int w, int h);   // h>w && h%w==0 ? h/w : 1
```
This is the entire contract between exporter and consumers — no filename convention,
no sidecar.

### Exporter (Studio scene)

Add a **sheet: N frames** cycle (4/8/16) and an **Export Sheet** button; export writes
`sprites/sheet_NN.hrt` (slot counter). No sidecar (shape carries the count).

### Consumers

- `--sandbox`: drop the hard-coded `sheet_frames_` map; load `spin_8` + probe
  `sprites/sheet_00..07.hrt`; on Tex-cycle set `s->frames = frames_in_sheet(img.w,img.h)`.
- `--anim`: prefer `sprites/sheet_00.hrt`, fall back to `spin_8`, derive `frames_`
  from the shape.

## Testing

- `test_flipbook`: `frames_in_sheet` (spin_8 → 8, 64×256 → 4, square/wide/non-multiple/
  zero → 1).
- `test_studio` (`test_make_sheet`): dims `size × N·size`, `h/w == N`, frame 0 == base,
  a later frame differs (scrolled) and reuses base colours, `frames<=1` → plain texture.
- End-to-end smoke (dev): `make_sheet → encode → decode → frames_in_sheet == N` for
  N ∈ {4,8,16}.

## Deferrals (ponytail)

Re-editable sheet recipes (recipe has no `frames` field); non-scroll animations
(pulse / palette-cycle / rotate — need real per-frame gen); scroll-direction control;
directory-scan discovery of sheets (fixed slot probe for now, no `<filesystem>`).
