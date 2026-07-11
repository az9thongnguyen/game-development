# Chapter 75 — The Texture Lab: an interactive studio scene

Chapters 73–74 gave us a generator: parameters in, a seamless texture out. But a
generator you can only drive by editing C++ and recompiling is a *library*, not a
*tool*. This chapter wraps it in the first **Mini Studio** scene — `./build/demo
--studio` — where you turn knobs, watch the texture update live, and **save** it into
the asset library for any game to load.

It is the moment the FPS wall textures from chapter 17 (hard-coded in
`fps::make_wall_textures`) become **parametric, interactive, and reusable**.

---

## 1. Two layers: pure core, thin scene

The studio splits cleanly along the engine's usual seam:

```
   studio_core  (PURE — no SDL, unit-tested headless)
   ┌───────────────────────────────────────────────┐
   │ noise.cpp  →  texture_gen.cpp  →  recipe.cpp    │
   │  (ch.73/74)   params → Image      params ⇄ text │
   └───────────────────────────────────────────────┘
                        ▲   │
        engine/image:   │   │  gfx::Image, encode_hrt
                        │   ▼
   StudioScene  (draws via Renderer2D + ui::Context, saves via assets::)
```

Everything hard (the maths) is in the pure core and tested without a window
([`tests/test_studio.cpp`](../../tests/test_studio.cpp)). The scene is only glue: read
the mouse, draw widgets, blit the preview, write files. This is why the core has
seven passing tests and the scene has none — there is nothing risky left in it.

---

## 2. The generation pipeline

`generate(const TextureParams&)` in
[`texture_gen.cpp`](../../src/games/studio/texture_gen.cpp) is three stages per pixel:

```
   sample(base)  ──▶  apply_op  ──▶  ramp(lo, hi)  ──▶  ARGB pixel
   scalar [0,1]       scalar         colour
```

1. **base** — pick a scalar field: `FBM`, `Value`, `Perlin` (chapter 73), `Checker`,
   or `Wood`.
2. **operator** — reshape the scalar: `None`, `Threshold` (hard cut → stone blocks,
   spots), or `Contrast` (steepen around the midpoint).
3. **ramp** — map the final `[0,1]` scalar onto a colour between `lo` and `hi`. A
   noise field is greyscale; the ramp is what makes it *sand*, *stone*, *grass*, or
   *lava*.

```cpp
const double t = apply_op(sample(p, u, v), p);
im.pixels[y * p.size + x] = ramp(p.lo, p.hi, t);
```

Because every stage is a pure function of `(params, u, v)`, the whole texture is
deterministic — the first test asserts `generate(p) == generate(p)` byte-for-byte.

---

## 3. Closing the image seam: `encode_hrt`

The engine already *reads* `.hrt` images (`decode_hrt`, chapter 07). To *save* one we
needed the inverse. It lives next to its twin in
[`engine/image.cpp`](../../src/engine/image.cpp):

```cpp
std::vector<uint8_t> encode_hrt(const Image& img) {
    // "HRT1" | big-endian u32 width | big-endian u32 height | R,G,B,A per pixel
    ...
    for (Color c : img.pixels) {
        out.push_back(r_of(c)); out.push_back(g_of(c));
        out.push_back(b_of(c)); out.push_back(a_of(c));
    }
}
```

The round-trip test `decode_hrt(encode_hrt(img)) == img` proves the byte order matches
its reader exactly. Saving goes through the **asset seam** (`assets::write_file`), the
same one-place-for-I/O rule from chapter 07 — so the day we add web persistence, only
that one function changes.

---

## 4. Recipes: non-destructive editing

If we saved only the pixels, a texture would be a dead end — you could never tweak it
again. So each save writes a tiny **recipe** sidecar next to the `.hrt`:

```
studio_00.hrt      ← the flat pixels (what games load)
studio_00.recipe   ← the parameters (what the studio re-opens)
```

The recipe is plain `key=value` text
([`recipe.cpp`](../../src/games/studio/recipe.cpp)) — no JSON library, per the
thin-shim rule. Reloading a recipe restores the exact `TextureParams`, so editing is
**non-destructive**: you reopen `studio_00`, nudge the frequency, and re-save. The
test asserts `to_recipe(from_recipe(text)) == text` and that reparsed params
regenerate identical pixels.

> **ponytail:** plain text, not a serialization format. Upgrade to the SDK's
> `json.hpp` only if a recipe ever needs nesting. It doesn't yet.

---

## 5. The live preview and the immediate-mode UI

The scene ([`studio_scene.cpp`](../../src/games/studio/studio_scene.cpp)) draws over
the immediate-mode `ui::Context` from chapter 48. Three ideas make it work:

**Throttled regeneration.** Regenerating 128² pixels *every frame* would be wasteful.
Instead a `dirty_` flag is set only when a widget reports a change, and the texture is
rebuilt once, after the UI pass:

```cpp
if (ui_.slider("frequency", freq_f_, 1, 32)) dirty_ = true;   // slider returns "changed"
...
if (dirty_) { regenerate(); dirty_ = false; }
```

**One `begin/end`, two panels.** The `ui::Context` holds one hot/active widget across
the frame. Calling `begin()` twice per frame would throw away the first panel's
interaction state. So both panels — TEXTURE LAB and COLLECTION — live inside a single
`begin()`…`end()`, each opened with its own `panel()` rect.

**Unique widget ids.** A widget's id is a hash of its *label* (chapter 48's contract),
so two buttons with the same text collide. The collection lists saved files, which
could repeat, so each button label gets a `##<index>` suffix — distinct id, at the
cost of the `##` showing in the text (a known, deferred cosmetic).

**Seeing the seam.** The preview blits the texture in a **2×2 grid**. If it tiles, the
four copies form one continuous field; if it doesn't, a cross-shaped seam jumps out —
the fastest possible visual test of chapter 74.

```
 ┌────┬────┐   TEXTURE LAB          COLLECTION
 │ ▓▒░│▓▒░ │   base: FBM            studio_00
 │ ░▒▓│░▒▓ │   op: None            studio_01
 ├────┼────┤   ramp: sand
 │ ▓▒░│▓▒░ │   [freq ====o    ]
 │ ░▒▓│░▒▓ │   [Save]
 └────┴────┘
```

---

## 6. Web note

The scene compiles and runs in the WASM build unchanged. But `assets::write_file` on
the web hits Emscripten's *in-memory* filesystem, which is lost on reload — so the web
build is **preview-only** for now. Persisting the collection across sessions (IDBFS)
is deferred; when added, again only the asset seam changes, not the scene.

---

## Pitfalls

- **Regenerating every frame** instead of on change — smooth on a 128² texture, but a
  512² one would stutter. Keep the `dirty_` gate.
- **Two `begin()` calls per frame** — silently breaks clicking on the second panel.
- **Colliding widget labels** — a cycling button whose label changes is fine (the click
  resolves before the label updates), but *duplicate* labels swallow clicks.
- **Blitting from a resized `Image`** — the preview `Sprite` points into
  `preview_.pixels`; regenerate replaces the vector, so build the `Sprite` *after*
  the `if (dirty_)` block each frame (the scene does).

## Glossary

- **Recipe** — the `key=value` sidecar storing a texture's parameters for re-editing.
- **Dirty flag** — a boolean that defers expensive work until an input actually changes.
- **Immediate-mode UI** — widgets that draw and report interaction in the same call
  (chapter 48); state lives in the caller.

## Exercises

1. **Export size.** Add a `size` cycle button (64/128/256) and confirm a 256² texture
   still tiles. *(Hint: `generate` already clamps and honours `size`; only the UI is
   missing.)*
2. **A real colour picker.** Replace the four preset ramps with `lo`/`hi` RGB sliders.
   *(Hint: six `slider` calls feeding `params_.lo/hi`; watch the label uniqueness.)*
3. **Load into a game.** Save a texture, then point an FPS wall at
   `assets/textures/studio_00.hrt` via `gfx::load_image`. *(Hint: the raycaster samples
   any `gfx::Image` — chapter 17 — so a studio texture drops straight in.)*
4. **Directory-backed collection.** Make the COLLECTION panel scan `assets/textures/`
   on startup instead of only listing this session's saves. *(Hint: `<filesystem>` on
   native; note it won't work on the web — a good reason it's deferred.)*

This completes Mini Studio v1. The textures you make here are the raw material the
Sandbox editor (a later chapter) will let you drag onto a scene and bring to life.
