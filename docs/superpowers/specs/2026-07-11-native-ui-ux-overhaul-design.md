# Native UI/UX Overhaul — Design Spec

> **Status:** approved 2026-07-11. Scope: the **native (software-rendered) UI/UX**
> of the engine and its games. The web admin dashboard is a *separate* sub-project
> (its own spec later).

## 1. Problem

Two complaints, on the same surface (the CPU software renderer):

1. **"Răng cưa / vỡ pixel"** — everything the renderer draws is aliased.
   - `Renderer2D::draw_line` is Bresenham → hard stair-steps, no coverage.
   - `Renderer2D::draw_char` is an 8×8 bitmap font scaled by integer
     block-replication → chunky letterforms. This is the most visible offender:
     text is in every HUD.
   - Only `fill_rect`/`draw_rect` exist — sharp 1px corners, no rounded/AA shapes.
   - The present path already offers linear filtering, but games run at 1:1
     (960×600), so the aliasing is baked into the *primitives*, not the upscale.

2. **"Chán / không thuyết phục"** — the UI (`engine/ui/ui.cpp`) is a reasonable
   dark palette but **flat**: no elevation, no rounded corners, no type hierarchy
   (all text is 8px white), no single accent, ad-hoc spacing. It reads as
   "programmer default", not a design system.

## 2. Goals / non-goals

**Goals**
- Remove visible aliasing from lines, shapes, and text.
- Replace the bitmap font with a real anti-aliased font at a proper type scale.
- Introduce a **design-system** layer (tokens + upgraded widgets) with the
  approved "modern dark + one accent" look.
- Do it at the **engine layer** so all six games inherit it; showcase on the two
  most UI-heavy games.
- Preserve **web (Emscripten) parity** and the **headless UI tests**.

**Non-goals (this spec)**
- The web admin dashboard (next sub-project).
- Per-game art/content redesign beyond HUD chrome.
- A full icon set (a handful of glyphs is fine).
- New gameplay features.

## 3. Decisions (from brainstorming)

| Question | Decision |
|---|---|
| Anti-aliasing | **Both**: SSAA (universal base) **+** per-primitive AA (Xiaolin Wu lines + coverage-based rounded-rect/circle). They compose. |
| Font | **stb_truetype** — real AA glyphs, single-header public-domain lib (in the spirit of "SDL as a thin shim"). |
| Aesthetic | **Modern dark + one accent** (Linear/Material-like: neutral dark surfaces, one hot-action accent, soft shadow, rounded corners, clear type hierarchy). |
| Showcase | **colony** (flagship demo, native+web) + **editor** (`--gui`/`--editor`, most widget-heavy). Other games inherit engine defaults. |
| UI font | **Inter** (SIL OFL) as the UI face + an optional monospace face for stat numbers. |

## 4. Architecture — three pillars

```
scene → ui::Context (theme tokens) → Renderer2D (AA primitives + font atlas)
      → framebuffer (ss×) → present (downsample via SDL linear) → screen
```

### Pillar 1 — Anti-aliasing

**SSAA (supersample).** `Renderer2D` gains an internal supersample factor `ss ∈
{1,2}`. The framebuffer and SDL present-texture are allocated at `ss×` the logical
size. **Game code keeps logical coordinates** — the renderer scales incoming
coords/sizes by `ss` internally, so no game layout constant changes. Present
downsamples `ss×logical → window` via the existing SDL linear filter
(`backend_sdl.cpp` `SDL_HINT_RENDER_SCALE_QUALITY=linear`). The 3D raycaster and
`renderer3d` fill the enlarged buffer automatically, so they get SSAA for free —
at 4× fill cost, so `ss` is a **toggle** (default `ss=2` for 2D scenes; `ss=1` or
on-demand for heavy 3D scenes).

- Coordinate rule: every `Renderer2D` primitive multiplies `(x,y,w,h)` and text
  size by `ss_` at entry. Clipping uses the physical (`ss×`) framebuffer bounds.
- Present: texture created at physical size; `SDL_RenderCopy` scales it to the
  window with linear filtering ≈ a box downsample.

**Per-primitive AA.** New primitives that write *partial coverage* as alpha
through the existing `blend()`:
- `draw_line_aa(x0,y0,x1,y1,c)` — Xiaolin Wu (endpoint handling + fractional
  coverage on the two straddling pixels per step).
- `fill_round_rect(x,y,w,h,r,c)` / `draw_round_rect(...)` — analytic corner
  coverage (distance-to-corner-center → coverage in the corner quarter-circles;
  straight edges are solid spans).
- `fill_circle(cx,cy,r,c)` / `draw_circle(...)` — same coverage idea.
These stay crisp even when `ss=1` (e.g. a 2D HUD over a 3D scene). Under SSAA they
compose correctly (coverage-alpha then downsample).

**Files:** `engine/renderer2d.{hpp,cpp}`, `platform/backend_sdl.cpp`,
`platform/platform.hpp` (config field `supersample`), `engine/app.*` (thread `ss`
to the renderer / present size).

### Pillar 2 — Font system (stb_truetype)

New module `engine/text/font.{hpp,cpp}`:
- Vendors `third_party/stb_truetype.h` (single header, public domain).
- Loads a `.ttf` via the `assets::load_file` seam (bytes → `stbtt_fontinfo`).
- Rasterizes an **AA glyph atlas per (face, pixel-size)** for printable ASCII
  (32–126): each glyph is an 8-bit coverage bitmap packed into one atlas buffer;
  metrics (advance, bearings, kerning-free for now) stored per glyph.
- API:
  ```cpp
  struct Font;                                   // opaque handle (face + size caches)
  Font* load_font(const std::string& ttf_path);  // nullptr on failure
  int   text_width (Font*, int px, const char* s);
  int   line_height(Font*, int px);
  // drawing goes through Renderer2D (below), which owns the target framebuffer
  ```
- Atlas cache keyed by `(Font*, px)`; built lazily on first use of a size.
- `Renderer2D::draw_text/draw_char` are **re-backed** by the font system: a
  Renderer2D holds a current `Font*` + size; `draw_text` blits AA glyph coverage
  as `color × coverage` alpha. The embedded **8×8 bitmap font stays as a
  fallback** if no `.ttf` loaded (so headless/no-asset paths never crash).

**Type scale (px, logical):** 12 caption · 14 body · 16 label · 20 title · 28
display. **Fonts shipped:** `assets/fonts/Inter-Regular.ttf`,
`assets/fonts/Inter-Bold.ttf`, and a monospace (`assets/fonts/*Mono*.ttf`) for
stat numbers.

**Web:** stb_truetype is portable C — compiles under Emscripten unchanged. The
font files are served through the asset VFS (Emscripten `--preload-file assets`),
same as existing assets.

**Files:** `third_party/stb_truetype.h`, `engine/text/font.{hpp,cpp}`,
`engine/renderer2d.{hpp,cpp}` (font-backed text + `set_font`), `assets/fonts/*`,
`CMakeLists.txt` (+ web preload).

### Pillar 3 — Design system (theme tokens + widgets)

New `engine/ui/theme.hpp` — **tokens** (single source of truth):
- **Surfaces:** `bg #12141C`, `elevated #1B1E28`, `border #2A2E3A`.
- **Text:** `primary #E6E9F0`, `secondary #A8AEBE`, `muted #6B7180`.
- **Accent (one hot-action):** `accent #5AAAE6`, `accent_hover #82C8FF`,
  `accent_press #3E86BE`.
- **Semantic:** `success #4CC38A`, `warn #E5B454`, `danger #E5657A`.
- **Spacing scale:** 4 / 8 / 12 / 16 / 24.
- **Radius:** `sm 6`, `md 10`.
- **Type scale:** the sizes above.
- **Elevation:** soft drop-shadow (offset + spread + alpha) for panels/popovers.

`engine/ui/ui.cpp` widgets rebuilt on tokens + AA primitives, **interaction logic
(hot/active/hovering) unchanged** so the headless tests still pass:
- **panel** = rounded-rect (`radius md`) + soft drop-shadow + optional title bar
  (elevated surface strip + `title` in `title` size).
- **button** = rounded (`radius sm`), states idle→hover→active→disabled; a
  `primary` variant uses `accent`; label centered in `label` size.
- **slider** = rounded track + filled portion in accent + rounded knob with a
  hover glow; value in the monospace face.
- **checkbox** = rounded box + accent check.
- **label / section header** = `body`/`secondary` and `title`/`primary`.
- Consistent padding/gap from the spacing scale (replaces the ad-hoc `kPad/kGap`).

**Files:** `engine/ui/theme.hpp`, `engine/ui/ui.{hpp,cpp}`.

### Application (showcase)

Engine changes benefit all games automatically (all use `Renderer2D`/`ui`). Then:
- **colony** (`colony_scene.cpp`): re-lay the HUD/panels on the new widgets +
  type scale; verify native **and** web.
- **editor** (`editor_scene.cpp`): the widget-heaviest surface — validate every
  widget variant.
Other games get the new defaults with only spot fixes if something looks off.

## 5. Error handling / fallbacks

- Font `.ttf` missing/parse-fail → `load_font` returns `nullptr`; `Renderer2D`
  falls back to the embedded 8×8 font and logs once. No crash.
- `ss=1` → per-primitive AA still active (quality degrades gracefully, never
  breaks).
- Null-renderer (headless) UI path unchanged → existing `test_ui` stays green.
- Atlas allocation failure for a size → that size falls back to 8×8; logged.

## 6. Testing

- **Unit (no SDL):**
  - Wu line: endpoint pixels are full-coverage; a shallow diagonal deposits
    fractional coverage on both straddling rows; total energy ≈ length.
  - Round-rect coverage: interior fully covered; corner alpha decreases
    monotonically outward; outside the radius = 0.
  - Font metrics: `text_width` strictly increases with string length for a fixed
    size and scales up with size; a rasterized glyph atlas for 'A' is non-empty.
  - Theme: token sanity (accent ≠ surface; text/bg contrast ratio above a floor).
- **Golden framebuffer:** render a fixed scene (panel + button + slider + a text
  line) into an offscreen `Framebuffer` and dump a PPM for eyeball review; assert
  a stable checksum so accidental regressions are caught.
- **Perf sanity:** colony at `ss=2` stays within frame budget on desktop; log the
  frame time; document the 3D `ss` fallback.
- **Sanitizers:** ASan/UBSan clean across new code.
- **Web:** build the Emscripten target and verify font + AA render in-browser
  (chrome-devtools MCP) for colony.
- Keep all existing CTest suites green.

## 7. Guidebook (mentor mode)

New focused chapters (split, per the docs convention — small clear files):
- **Anti-aliasing I — SSAA & the supersample seam** (why 2×, downsample math,
  the coord-scale rule, the 3D toggle & cost).
- **Anti-aliasing II — Xiaolin Wu lines & coverage rasterization** (the coverage
  concept, Wu's error term, analytic corner coverage).
- **Font rendering — stb_truetype & the glyph atlas** (outlines vs bitmaps, AA
  rasterization, atlas caching, metrics, the fallback).
- **Design systems — tokens, type scale & the widget layer** (what a design
  system is, the token model, states, elevation, applying it to the IMGUI).

## 8. Sequencing (each phase = a milestone branch, merged `--no-ff`)

1. **Font system** — stb_truetype + atlas + Renderer2D text re-backing + fallback
   (biggest visible win first).
2. **Anti-aliasing** — SSAA seam + Wu lines + coverage shapes.
3. **Design system** — theme tokens + widget upgrade.
4. **Apply + docs** — colony + editor HUDs; guidebook chapters; web verification.

## 9. Risks / open items

- **Font file provenance:** download Inter (OFL) + a mono via `curl` and commit
  under `assets/fonts/` with their licenses; if network is unavailable at build
  time it's already vendored in the repo.
- **SSAA perf on 3D:** mitigated by the `ss` toggle (default `ss=1` for the
  raycaster/`renderer3d` scenes, `ss=2` for 2D).
- **Atlas memory:** ASCII-only, a few sizes, per face — kilobytes; fine. Unicode
  is out of scope.
- **Web preload size:** three TTFs add a few hundred KB to the WASM payload;
  acceptable; could subset later (noted, not done).
