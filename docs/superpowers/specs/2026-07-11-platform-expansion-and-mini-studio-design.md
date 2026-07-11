# Platform expansion — roadmap + Sub-project 1: Mini Studio (Texture Lab)

**Date:** 2026-07-11
**Status:** design / checkpoint (awaiting build)
**Author:** brainstorming session (auto-approve standing)

---

## 0. Framing & the one load-bearing assumption

The request spans **three big, independent tracks**: (A) a stronger engine, (B) a
content-authoring "mini studio", (C) a bigger backend-as-a-service (provision,
marketplace, sandbox, test-runs). Each deserves its own spec → plan → build cycle;
this document decomposes all three and then designs **the first sub-project in
depth**, per the standing "decompose, spec one at a time" working style.

**Assumption (stated so it can be overridden):** this stays a **learning-first,
single-operator platform** — the documented #1 goal is "học để hiểu sâu và tự làm
được". So the BaaS asks ("marketplace", "provision game", "sandbox") are built as
**lean, hand-written mechanisms for one operator (you)**, NOT a commercial
multi-tenant SaaS for external developers. That keeps ~1/3 of the wishlist from
ballooning into account systems, billing, moderation, and infra we'd never finish.
If the real goal is a product other developers use, say so and Track C changes shape.

### What already exists (do not rebuild)
- **Engine:** software renderer 2D+3D, math, geometry, camera, ECS, jobs, allocators,
  2D physics, immediate-mode GUI (`ui::Context`), fonts/AA, asset pipeline + hot reload.
- **Games:** chess, fps, viz3d sandbox, iso farm, editor, colony.
- **BaaS (mature):** auth, leaderboard, cloud save, inventory, remote config,
  analytics, live events, realtime (WS lobby/matchmaking), replays, rate limit,
  metrics, admin dashboard, multi-tenant projects, C++ SDK, WASM port.

This is **targeted expansion, not a rewrite.**

---

## 1. The three tracks (decomposition)

### Track A — Engine depth ("not just 2D/3D")
"Not just 2D/3D" is vague and needs your pick before it can be specced. Candidate
modules, each its own later spec (thorough guidebook chapter each):
- Lighting: point/directional lights on the 3D rasterizer (already have per-face
  normals + Gouraud), simple ambient+diffuse; optional specular.
- CPU particle system (emitters, pooled via the frame allocator).
- Sprite/skeletal animation (2D frame animation first; skinned mesh later).
- Material system (named parameter sets a mesh/quad references).
- Spatial/positional audio (pan+attenuation over the existing audio seam).
- Post-FX on the framebuffer (vignette, bloom-lite, color grading).

**Needs input:** which of these matters most. Flagged, not scheduled.

### Track B — Mini Studio (content authoring) — **first track, this spec**
One studio scene shell (`--studio`) hosting sibling generator modules that all
produce engine-native assets (`gfx::Image` / meshes / tilemaps) and save them into
the asset library:
1. **Texture Lab** (v1 — designed below): procedural textures (noise + operators),
   seamless tiling, save to `.hrt`, re-editable recipes, a collection browser.
2. Map / tilemap generator (noise → tiles + autotiling) — later spec.
3. Isometric scene composer (paint + object placement, reuses `iso_core`) — later.
4. 3D mesh / primitive lab (extend the viz3d sandbox to export meshes) — later.
5. Collections manager (browse/tag/reuse everything above) — later.

**Why Track B is first** (alternatives weighed):
- *Engine first (A):* high value but blocked on your "which feature" pick, less
  self-contained, and produces nothing the other tracks can consume.
- *BaaS first (C):* depends on the learning-vs-product question AND on having content
  to distribute — premature before the studio makes any.
- *Studio first (B):* most self-contained and most visual; purest "learn by
  building"; **generalizes an existing pattern** (`fps::make_wall_textures` already
  generates textures into `gfx::Image`); reuses `ui::Context` + the asset seam; and
  it **produces the assets the other two tracks need** (engine consumes them, BaaS
  distributes them). It is the spine that connects all three. → **Recommended first.**

### Track C — BaaS platform extensions (lean, single-operator)
Interpreted through the learning-first assumption; each its own later spec:
- **Provision game:** a "new game" wizard on top of the *existing* admin project API
  + starter templates (scene + config). Mechanism mostly exists; add UX + templates.
- **Asset registry ("marketplace"):** a BaaS service storing/serving the studio's
  `.hrt` collections per project (upload/list/download); a "public" collection is the
  "marketplace". Reuses the controller→service→SDK-handle→test pattern. It's an asset
  CDN/registry — **not** a commerce/payments system.
- **Sandbox / manage test-runs:** a dashboard area that launches a project's WASM
  build with a chosen scene + config and captures logs/metrics (reuses observability).

---

## 2. Sub-project 1 design — Mini Studio: **Texture Lab**

### 2.1 Goal & non-goals
**Goal:** `./build/demo --studio` opens an interactive tool to author textures
procedurally and save them into `assets/textures/` as `.hrt`, so any scene can load
them. It turns today's hardcoded `make_wall_textures()` into a parametric,
interactive, re-editable, saveable generator.

**Non-goals (YAGNI now, noted for later):** freehand pixel painting, PBR/material
maps, layer masks, undo history, web (IDBFS) persistence, importing external images.

### 2.2 Architecture
```
[ studio::StudioScene ]  (--studio ; a Scene, drawn by Renderer2D + ui::Context)
        |  owns params + preview Image + collection list
        v
[ studio_core ]  (STATIC lib, PURE — no SDL, unit-tested headless)
   - noise.{hpp,cpp}       value noise, Perlin, fBm  (tileable variants)
   - texture_gen.{hpp,cpp} TextureParams -> gfx::Image  (base pattern + operators)
   - recipe.{hpp,cpp}      TextureParams <-> text sidecar (re-editable)
        |
        v  (save/load through the existing asset seam)
[ engine/image ]  decode_hrt (exists) + encode_hrt (NEW)  <-> assets::write_file/load_file
```
The studio shell mirrors the existing `--editor`/`viz3d` scenes: a left parameter
panel, a center live preview (blit the generated `gfx::Image`, shown 2×2 to prove
seamlessness), a right collection browser listing `assets/textures/*.hrt`.

### 2.3 Data model
```cpp
struct TextureParams {
    uint32_t seed = 1;
    int      size = 128;          // square; 64/128/256
    enum Base { Value, Perlin, FBM, Checker, Bricks, Wood } base = FBM;
    float    frequency = 4.0f;    // lattice cells across the tile (integer for tiling)
    int      octaves   = 4;       // fBm
    float    gain      = 0.5f;    // fBm amplitude falloff
    float    lacunarity= 2.0f;    // fBm frequency growth
    bool     tileable  = true;    // wrap the lattice on a torus -> seamless
    Color    lo{...}, hi{...};    // ramp endpoints (scalar noise -> color)
    enum Op { None, Threshold, Contrast } op = None;
    float    op_amount = 0.5f;
};
gfx::Image generate(const TextureParams&);   // deterministic, pure
```
- **Seamless tiling (lazy-correct choice):** evaluate value/Perlin noise on a torus —
  wrap lattice coordinates modulo `frequency` so the left/right and top/bottom edges
  match exactly. (Wang/corner tiles are a documented future option; periodic-lattice
  is enough and simpler for v1.)
- **fBm** = sum of `octaves` noise layers, each `frequency *= lacunarity`,
  `amplitude *= gain`, normalized to [0,1].

### 2.4 Persistence
- **NEW `encode_hrt(const gfx::Image&) -> std::vector<uint8_t>`** in `engine/image.*`
  (mirrors the existing `decode_hrt`; same `HRT1|BE w|BE h|RGBA8` format). Save with
  `assets::write_file("textures/<name>.hrt", bytes)`.
- **Recipe sidecar:** `assets::write_file("textures/<name>.recipe", ...)` — a tiny
  `key=value\n` text dump of `TextureParams` (hand-parsed; no JSON lib). Loading a
  texture in the browser reads the recipe back into the params → **non-destructive,
  re-editable**. `ponytail:` plain key=value text, upgrade to the SDK's json.hpp only
  if a nested schema ever appears.
- **Collection** = the `assets/textures/` directory. v1 lists files by scanning the
  directory natively; the browser shows name + a thumbnail (the loaded image).

### 2.5 UI (immediate-mode, existing `ui::Context`)
- Left panel sliders: frequency, octaves, gain, lacunarity, seed, op_amount; buttons
  to cycle Base and Op; r/g/b sliders for `lo`/`hi`; `Randomize`, `Save`, `New`.
- **Regeneration is throttled:** regenerate the preview only when a control reports a
  change (widgets return "changed"), not every frame — 128² noise per edit is cheap.
- Center: preview blit + 2×2 tiled view. Right: one button per saved file (labels
  suffixed with an index — the `ui` id is a label hash, labels must be unique).

### 2.6 Web (WASM)
The scene compiles and runs in the WASM build (same code). `assets::write_file` on
web hits Emscripten's in-memory FS (lost on reload) — **v1 targets native for
authoring; web is preview-only.** IDBFS persistence is future work (noted, not built).

### 2.7 Testing (`tests/test_studio.cpp`, CTest suite `studio`)
- **Determinism:** `generate(p) == generate(p)` byte-for-byte; changing `seed` changes
  output.
- **Seamlessness:** with `tileable=true`, edge column 0 == column w-1 and row 0 ==
  row h-1 (exact for value/Perlin lattice wrap).
- **Round-trip:** `decode_hrt(encode_hrt(img)) == img` (closes the image seam).
- **Recipe round-trip:** `parse(serialize(p)) == p`.
- **fBm sanity:** output stays in [0,1]; more octaves ⇒ strictly ≥ variance of 1 octave.
Headless — compiles `image.cpp`/`assets.cpp` directly (the established dependency-free
test pattern), no SDL/window.

### 2.8 Build (CMake)
- `studio_core` STATIC (`noise.cpp`, `texture_gen.cpp`, `recipe.cpp`), `PUBLIC src`.
- `studio/studio_scene.cpp` added to the `demo` exe sources; `demo` links
  `studio_core` (it already links `ui_core text_core`).
- `test_studio` links `studio_core engine_flags` + compiles `image.cpp`,`assets.cpp`;
  `add_test(NAME studio ...)`.
- `main.cpp`: add `--studio` → `platform::Config` (960×600, crisp, SSAA=kAA) +
  `std::make_unique<studio::StudioScene>()`.

### 2.9 Integration (prove the loop)
After save works, load a studio-made `.hrt` in one existing scene (e.g. an `--fps`
wall or a `--3d` cube face) to demonstrate the produce→save→consume loop end-to-end.

### 2.10 Guidebook (thorough, split small — per working style)
- ch.73 Procedural noise: value → Perlin → fBm (theory, ASCII lattice diagram, worked
  numbers, pitfalls, exercises).
- ch.74 Seamless tiling on a torus.
- ch.75 The Texture Lab scene: params, live preview, save + recipes + collection.

### 2.11 Build steps (one commit each, on `feat/studio-texture-lab`)
1. `noise.*` + tests (value/Perlin/fBm, tileable) → ch.73/74.
2. `texture_gen.*` (base patterns + ramp + operators) + tests.
3. `encode_hrt` + `recipe.*` + round-trip tests.
4. `studio_scene.*` + `--studio` wiring + `ui` panel + live preview + collection.
5. Integration: load a generated `.hrt` in an existing scene → ch.75.
6. Review (cpp/security), ASan/UBSan, merge `--no-ff` to `main`.

---

## 3. Open decisions for you (I proceed on the defaults unless you redirect)
1. **Intent** — learning-first single-operator (assumed) vs. product for other devs.
2. **First track** — Mini Studio / Texture Lab (recommended, assumed) vs. Engine (A)
   vs. BaaS (C).
3. **Engine pick** (only if Track A goes first) — which module from §1-A.
