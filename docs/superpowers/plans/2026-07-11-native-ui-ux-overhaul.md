# Native UI/UX Overhaul — Implementation Plan

> **For agentic workers:** implement task-by-task, TDD where logic is non-trivial,
> commit per step. Spec: `docs/superpowers/specs/2026-07-11-native-ui-ux-overhaul-design.md`.

**Goal:** Remove aliasing (SSAA + Wu/coverage AA), replace the bitmap font with an
AA stb_truetype font, and add a design-system theme/widget layer — at the engine
level, showcased on colony + editor, with web parity.

**Architecture:** `scene → ui::Context(tokens) → Renderer2D(AA + font atlas) →
framebuffer(ss×) → present(downsample) → screen`.

**Tech stack:** C++20, SDL2 (platform shim only), stb_truetype (vendored single
header), Inter + a mono TTF (OFL), CMake, Emscripten for web.

## Global constraints (verbatim from spec / project rules)
- SDL only inside `src/platform/`. Engine/game code never calls `SDL_`.
- All file I/O through `assets::` (web VFS parity). No raw `fopen`/`ifstream`.
- `-Wall -Wextra -Wpedantic`, ASan/UBSan clean.
- Explicit CMake source lists (no globbing).
- Game code uses **logical** coordinates; SSAA is internal to the renderer.
- Headless UI tests (null renderer) must stay green — interaction logic unchanged.
- Each phase = its own `feat/uiN-*` branch, merged `--no-ff` into `main`.

---

## Phase 1 — Font system (`feat/ui1-fonts`)

### Task 1.1 — Vendor stb_truetype + font assets
**Files:** `third_party/stb_truetype.h` (curl from nothings/stb), `assets/fonts/`
(Inter-Regular.ttf, Inter-Bold.ttf, a mono TTF, + LICENSE files).
- Download via `curl` (network verified available). Commit binaries + licenses.
- No test (asset step). Commit.

### Task 1.2 — Font module: load + metrics
**Files:** Create `src/engine/text/font.hpp`, `src/engine/text/font.cpp`;
Test `tests/test_font.cpp`; CMake: new `text_core` static lib (compiles
stb_truetype impl in one TU via `#define STB_TRUETYPE_IMPLEMENTATION`).

**Interfaces (Produces):**
```cpp
namespace text {
struct Glyph { int w,h,advance,bearing_x,bearing_y; const uint8_t* cov; }; // cov: w*h, 8-bit
class Font {
public:
  static Font* load(const std::string& ttf_path);   // nullptr on fail
  int  line_height(int px);
  int  text_width(int px, const char* s);
  const Glyph* glyph(int px, char c);                // builds/【caches】 the size atlas lazily
};
} // namespace text
```
- Implementation: `stbtt_InitFont` on the loaded bytes (kept alive in the Font).
  Per size: `stbtt_ScaleForPixelHeight`, rasterize ASCII 32..126 with
  `stbtt_MakeCodepointBitmap` into a per-glyph coverage buffer; store metrics via
  `stbtt_GetCodepointHMetrics` + `stbtt_GetFontVMetrics`.
- Size cache: `std::unordered_map<int, SizeAtlas>` inside Font.

**TDD:**
- [ ] Test: `Font::load` on the real Inter path returns non-null; on a bogus path
  returns null.
- [ ] Test: `text_width(px,"AB") > text_width(px,"A") > 0`; `text_width(24,s) >
  text_width(12,s)`.
- [ ] Test: `glyph(16,'A')` non-null, `w>0 && h>0`, and at least one coverage byte
  `>0`.
- [ ] Run (fails: no Font), implement, run (passes), commit.

### Task 1.3 — Re-back Renderer2D text with the font
**Files:** Modify `src/engine/renderer2d.{hpp,cpp}`; extend `tests/test_font.cpp`
(text into an offscreen framebuffer).

**Interfaces (Produces):**
```cpp
void Renderer2D::set_font(text::Font* f, int px);   // null → 8x8 fallback
void Renderer2D::draw_text(int x, int y, const char* s, Color c);      // font-backed
int  Renderer2D::text_width(const char* s) const;   // via current font/size
// keep legacy: draw_text(x,y,s,c,scale) delegates to the 8x8 path (fallback/tests)
```
- Font-backed `draw_text`: pen advances per glyph; each glyph blits `blend(dst,
  color_with_alpha(cov[i]))`. Newline resets x, adds `line_height`.
- If no font set OR glyph null → 8×8 fallback (existing code path).

**TDD:**
- [ ] Test: with a font set, `draw_text` into a cleared framebuffer writes some
  non-background pixels; with a known baseline the 'A' region is non-empty.
- [ ] Test: no font set → still draws via 8×8 (existing behavior preserved).
- [ ] Implement, run, commit.

### Task 1.4 — Wire font load into app/scenes + web preload
**Files:** `src/engine/app.*` or scene init (load default UI font once, set on
Renderer2D); `CMakeLists.txt` (link `text_core`; Emscripten `--preload-file
assets`); `cmake/emscripten.toolchain.cmake` note.
- One place loads `assets/fonts/Inter-Regular.ttf` at startup and sets it as the
  Renderer2D default at the body size.
- [ ] Build native; run colony — text is AA (manual check).
- [ ] Commit. Merge `feat/ui1-fonts` `--no-ff`.

### Task 1.5 — Guidebook chapter (font rendering)
**Files:** `docs/book/NN-font-rendering.md` — outlines vs bitmaps, AA rasterizing,
atlas caching, metrics, fallback. Commit (folded into the phase merge).

---

## Phase 2 — Anti-aliasing (`feat/ui2-aa`)

### Task 2.1 — SSAA seam (supersample factor)
**Files:** `src/platform/platform.hpp` (config `int supersample = 1;`),
`src/platform/backend_sdl.cpp` (framebuffer + texture at `ss×`, window at logical,
linear present), `src/engine/renderer2d.{hpp,cpp}` (`int ss_`; scale all coords +
text size by `ss_`; clip to physical bounds), `src/engine/app.*` (thread ss +
present size), `src/main.cpp` (set `ss=2` for 2D scenes, `ss=1` for 3D/raycaster).

**Design:** logical size stays the API; physical = `ss×logical`. `Renderer2D`
methods multiply `x,y,w,h` (and font px) by `ss_` on entry. `set_pixel`/
`blend_pixel` clip to physical `fb_.width/height`. Present: `SDL_RenderCopy`
texture(physical) → window(logical) with linear filter = box-ish downsample.

**TDD:**
- [ ] Test: a `Renderer2D{fb, ss=2}` given `fill_rect(0,0,1,1)` fills a 2×2
  physical block (coordinate scaling correct).
- [ ] Test: `ss=1` behaves exactly as today (regression guard).
- [ ] Implement, run, manual check (colony crisper), commit.

### Task 2.2 — Xiaolin Wu AA lines
**Files:** `src/engine/renderer2d.{hpp,cpp}` (`draw_line_aa`); `tests/test_aa.cpp`.
- Standard Wu: handle steep/shallow via axis swap; endpoints; per step
  `blend_pixel` two straddling pixels with coverage `= 1-frac` and `frac`, alpha =
  `coverage × src.a`.
- Helper: `blend_pixel_cov(x,y,Color c, float cov)`.

**TDD:**
- [ ] Test: horizontal/vertical lines deposit full-coverage pixels only (no
  bleed). Diagonal deposits fractional coverage on both rows at a sampled x;
  coverage pair sums ≈ 1. Endpoints full.
- [ ] Implement, run, commit.

### Task 2.3 — Coverage AA shapes (rounded-rect, circle)
**Files:** `src/engine/renderer2d.{hpp,cpp}` (`fill_round_rect`, `draw_round_rect`,
`fill_circle`, `draw_circle`); extend `tests/test_aa.cpp`.
- Rounded-rect fill: solid center span; the four corner quarter-circles use
  analytic coverage — for each corner pixel, `cov = clamp(r + 0.5 -
  dist(pixel_center, corner_center), 0, 1)`.
- Circle: same coverage vs radius.

**TDD:**
- [ ] Test: `fill_round_rect` interior center pixel full-coverage; a pixel just
  outside a corner radius = 0; a pixel straddling the arc is fractional; coverage
  decreases monotonically moving out along the corner diagonal.
- [ ] Implement, run, commit.

### Task 2.4 — Guidebook chapters (AA I + II)
**Files:** `docs/book/NN-antialiasing-ssaa.md`, `docs/book/NN-antialiasing-wu-coverage.md`.
- [ ] Commit. Merge `feat/ui2-aa` `--no-ff`.

---

## Phase 3 — Design system (`feat/ui3-designsystem`)

### Task 3.1 — Theme tokens
**Files:** Create `src/engine/ui/theme.hpp`; `tests/test_theme.cpp`.

**Interfaces (Produces):** `namespace ui::theme` with `constexpr gfx::Color`
surfaces/text/accent/semantic; `constexpr int space[5]`, `radius_sm/md`, type
scale `sz_caption..sz_display`; a `struct Shadow { int dx,dy,spread; uint8_t a; }`.
Values exactly as the spec §4 Pillar 3.

**TDD:**
- [ ] Test: `accent != bg`; simple luminance contrast(text_primary, bg) above a
  floor (≥ 4.5:1 proxy); spacing scale strictly increasing.
- [ ] Implement, commit.

### Task 3.2 — Soft shadow primitive
**Files:** `src/engine/renderer2d.{hpp,cpp}` (`fill_round_rect_shadow` or a
`drop_shadow(rect,radius,Shadow)` that lays a few translucent offset rounded rects
— cheap blur approximation); extend `tests/test_aa.cpp`.
- [ ] Test: shadow writes translucent pixels below/right of the rect, none fully
  opaque. Implement, commit.

### Task 3.3 — Rebuild widgets on tokens + AA
**Files:** Modify `src/engine/ui/ui.{hpp,cpp}`; keep `tests/test_ui*` green.
- panel → shadow + `fill_round_rect(elevated)` + border + title bar; button →
  `fill_round_rect` states (idle/hover/active/disabled) + optional `primary`
  (accent) + centered label in `label` size; slider → rounded track + accent fill
  + rounded knob + hover glow + mono value; checkbox → rounded + accent check;
  spacing from `theme::space`.
- **Interaction logic (hot/active/hovering/id) untouched.**
- Add a `button` `primary` overload or a `ButtonStyle` param (default normal).

**TDD:**
- [ ] Existing `test_ui` (headless, null renderer) still passes unchanged.
- [ ] Add: with a renderer, a `primary` button fills with accent; disabled button
  ignores clicks (returns false even when pressed+released over it).
- [ ] Implement, run, commit.

### Task 3.4 — Golden framebuffer test
**Files:** `tests/test_ui_golden.cpp` — render panel+button+slider+text into an
offscreen framebuffer; dump PPM to build dir; assert a stable checksum.
- [ ] Implement, run, commit.

### Task 3.5 — Guidebook chapter (design systems)
**Files:** `docs/book/NN-design-systems.md`. Commit. Merge `feat/ui3-designsystem` `--no-ff`.

---

## Phase 4 — Apply + web verify (`feat/ui4-apply`)

### Task 4.1 — Colony HUD on the new system
**Files:** `src/games/colony/colony_scene.cpp` (+ helpers). Re-lay panels/HUD on
the new widgets + type scale + accent; use mono for numbers.
- [ ] Build native, run `--colony`, manual check. Commit.

### Task 4.2 — Editor HUD (all widget variants)
**Files:** `src/games/editor/editor_scene.cpp`. Exercise every widget variant.
- [ ] Build, run `--gui`/`--editor`, manual check. Commit.

### Task 4.3 — Other games spot-fix
- Run `--chess`,`--fps`,`--iso`,`--3d`; fix only what looks broken by the new
  defaults (e.g. text sizing). Commit.

### Task 4.4 — Web build + in-browser verify
**Files:** `CMakeLists.txt`/toolchain (ensure `--preload-file assets` incl. fonts).
- `source ~/emsdk/emsdk_env.sh && emcmake cmake -B build-web && cmake --build
  build-web --target demo`; serve; verify colony in-browser (chrome-devtools MCP):
  AA + font render, no console errors.
- [ ] Commit.

### Task 4.5 — Overview/README + perf note + merge
**Files:** `docs/book/00-overview.md` (link new chapters), `README.md` if needed;
note the `ss` toggle + colony `ss=2` frame time.
- [ ] Full `ctest` green, ASan/UBSan clean. Commit. Merge `feat/ui4-apply` `--no-ff`.

---

## Self-review (plan ↔ spec)
- Spec §4 Pillar 1 (SSAA + Wu + coverage) → Phase 2 tasks 2.1–2.3. ✓
- Spec §4 Pillar 2 (font) → Phase 1. ✓
- Spec §4 Pillar 3 (tokens + widgets) → Phase 3. ✓
- Spec §4 Application → Phase 4. ✓
- Spec §6 testing (unit AA/font/theme, golden, perf, web, sanitizers) → 1.2/1.3,
  2.1–2.3, 3.1–3.4, 4.4. ✓
- Spec §7 guidebook → 1.5, 2.4, 3.5, 4.5. ✓
- No placeholders; interface names consistent across tasks (Font, Glyph,
  set_font, draw_text, draw_line_aa, fill_round_rect, theme::*).
