# Chapter 77 — Textured Sprites: Where Two Tools Meet

> Code: `src/games/sandbox/world.{hpp,cpp}`, `serialize.cpp`,
> `sandbox_scene.{hpp,cpp}`, `src/engine/renderer2d.{hpp,cpp}` (`blit_scaled`);
> tests `tests/test_sandbox.cpp`, `tests/test_aa.cpp`; run `./build/demo --sandbox`.

Two chapters ago the **Texture Lab** (ch.73–75) learned to *author an asset* and write
it to `assets/textures/studio_NN.hrt`. Last chapter the **Sandbox** (ch.76) learned to
*author a little world* out of coloured actors. This chapter is the seam between them:
a sandbox actor stops being a flat rectangle and starts *wearing* a texture the Lab made.

Nothing here is a new subsystem. It is one field, one renderer primitive, and one
discovery probe — and the interesting part is where each of those three things is
*allowed to live*, because the answer is dictated by rules the whole engine is built to
protect.

## 1. A texture is a name, not pixels

The obvious move is to hang a `gfx::Image` on the sprite. Resist it. The sandbox's model
layer — `world.hpp`, `world.cpp`, `serialize.cpp`, compiled into `sandbox_core` and
tested with **no window and no files** — must stay free of image decoding and I/O. So the
model gets the smallest thing that identifies a texture: its **name**.

```cpp
struct Sprite    { gfx::Color color = ...; bool round = false; std::string texture; };
struct Archetype { ...; std::string texture; };   // "" = flat colour (unchanged default)
```

An empty string is the old behaviour, byte-for-byte, so every actor and every existing
test is untouched. The name is a *reference*; resolving it to pixels is somebody else's
job — and that somebody lives below, in the one file allowed to touch the renderer and
the asset seam. This is the same discipline the platform seam enforces for SDL: the pure
layer names what it wants, a lower layer knows how to get it.

Because the field is just a string, it round-trips through the serializer for free — a
single token, emitted only when set:

```
e x=10 y=20 rot=0 color=dcc878 w=24 h=24 tex=studio_02
```

`archetype_tokens` appends ` tex=<name>` when the name is non-empty; `parse_archetype`
reads it back. An untextured actor emits **no** `tex=` token, so old scene files stay
identical and diffs stay quiet (`test_scene_roundtrip_texture` asserts both halves). And
since Play/Stop *is* a serialize/restore (ch.76 §"Play/Stop as a snapshot"), the texture
survives a play session with zero extra code.

## 2. `blit_scaled`: the missing renderer primitive

An actor's on-screen size is `Body` × `Transform2D.scale` — anything from a 6 px pellet
to a scaled-up 80 px sweeper. The Lab makes 128² textures. The existing `blit` copies a
sprite at its *native* size, so it cannot fill an arbitrary rect. We need a resample.

That resample is a *general* rendering capability, not a sandbox quirk, so it belongs in
`Renderer2D` where every game can reach it:

```cpp
void Renderer2D::blit_scaled(const Sprite& s, int dx, int dy, int dw, int dh) {
    if (!s.pixels || dw <= 0 || dh <= 0 || s.w <= 0 || s.h <= 0) return;
    for (int oy = 0; oy < dh; ++oy) {
        const int sy = oy * s.h / dh;                  // nearest source row
        for (int ox = 0; ox < dw; ++ox) {
            const int sx = ox * s.w / dw;              // nearest source col
            const Color src = s.pixels[sy * s.w + sx];
            if (a_of(src) == 0) continue;              // transparent texel: skip
            const int bx = (dx + ox) * ss_, by = (dy + oy) * ss_;
            for (int py = 0; py < ss_; ++py)
                for (int px = 0; px < ss_; ++px) blend_cov(bx + px, by + py, src, 255);
        }
    }
}
```

Three things worth naming:

- **Nearest-neighbour on purpose.** `sx = ox * s.w / dw` maps each destination column back
  to a source column with integer math — no filtering, no float, deterministic. A pixel-art
  texture *should* look blocky when magnified; a bilinear filter would be more code for a
  worse look here. (Upgrade path if you ever want smooth: sample four texels and lerp.)
- **It speaks logical coordinates.** Like every other primitive it multiplies by `ss_` at
  the end, so supersampling is invisible to the caller (ch.68). The sandbox never knows it
  is drawing into a 2× framebuffer.
- **It fails closed.** Null pixels or a degenerate rect is a silent no-op, not a crash —
  the draw loop must never fault on a half-built actor. `test_aa.cpp` pins the upscale
  blocks, the transparent-texel skip, and the `dw <= 0` no-op.

The draw path then chooses per actor:

```cpp
auto tit = s.texture.empty() ? tex_.end() : tex_.find(s.texture);
if (tit != tex_.end()) {
    gfx::Sprite spr{tit->second.pixels.data(), tit->second.w, tit->second.h};
    g.blit_scaled(spr, cx - dw/2, cy - dh/2, dw, dh);   // stretches with Body × scale
} else if (s.round) g.fill_circle(cx, cy, dw/2, s.color);
else                g.fill_rect(cx - dw/2, cy - dh/2, dw, dh, s.color);
```

## 3. Discovery without a directory scan

The scene has a name on an actor and a folder full of `.hrt` files. How does it learn
*which* names exist? The tempting answer — scan `assets/textures/` — pulls in
`<filesystem>`, which the web build does not have (assets there live in a virtual FS). The
Lab itself dodged this by keeping an in-session list; the sandbox is a different process
run, so it must rediscover.

The lazy, portable answer is to **probe the Lab's own naming convention**:

```cpp
void SandboxScene::load_textures() {
    for (int i = 0; i < 32; ++i) {
        char name[16]; std::snprintf(name, sizeof(name), "studio_%02d", i);
        auto img = gfx::load_image(std::string("textures/") + name + ".hrt");
        if (img) { tex_names_.push_back(name); tex_[name] = std::move(*img); }
    }
}
```

`load_image` returns `std::nullopt` for a missing or malformed file, so the loop is
self-limiting: it keeps whatever the Lab actually saved and silently ignores the rest.
It works identically native and on WASM because it only ever asks the asset seam for a
*named* file — never for a *listing*. The ceiling is honest and marked in the code: 32
names. When a real project outgrows that, the upgrade is a manifest file the Lab writes on
save, not a platform-specific directory walk.

This is why both tools point at the same `assets` base (`main.cpp` calls
`assets::set_base_path("assets")` once for every scene): the Lab *writes*
`textures/studio_00.hrt` and the sandbox *reads* `textures/studio_00.hrt` through the
exact same seam. Save in `--studio`, quit, launch `--sandbox`, and it is there.

## 4. Assigning a texture

Selection and the inspector already exist (ch.76). Texturing adds one control: a **`Tex:`**
button that cycles `none → studio_00 → studio_01 → … → none`, writing the chosen name into
the selected actor's `Sprite.texture`. When no textures were discovered the button is
replaced by a muted `(make textures in --studio)` hint — the feature announces its own
prerequisite instead of silently doing nothing. Recolor still works; it just isn't visible
while a texture covers the fill.

## 5. Where each piece lives (the whole point)

```
                 names a texture           resolves the name
   world.hpp  ───────────────────►  sandbox_scene.cpp ──► load_image ──► assets:: (seam)
   (pure core: string only)         (the ONLY file that                 │
        │                            touches renderer + IO)             ▼
        │  round-trips the name                        blit_scaled ──► Renderer2D
        ▼                                               (general primitive, all games)
   serialize.cpp  ──►  "… tex=studio_02"
```

The same shape you have seen all through this engine: the layer that *decides* names the
thing; the layer that *can* fetches it. Keep the decision pure and testable, push the
capability down to where it is allowed to live, and the two halves compose without either
knowing the other's internals. That is the entire trick to letting two independently-built
tools — a texture generator and a world editor — suddenly work together.

## Pitfalls

- **Putting the `gfx::Image` in the core.** It compiles, then the headless test target
  won't link (no image/asset code there by design) and the web story rots. The core names;
  it does not fetch.
- **Reaching for `<filesystem>` to list the folder.** Native-only. Probe names through the
  asset seam instead.
- **Forgetting the transparent-texel skip in `blit_scaled`.** Textures with alpha (future
  cut-out sprites) would paint their background over the scene. `a_of(src) == 0 → continue`.
- **Stretch surprises.** A non-square texture on a square actor stretches; the Lab makes
  square textures, so it is a non-issue today, but it is a real limit, not a bug.

## Glossary

- **`.hrt`** — the engine's hand-parsed raster format (ch. on `image.hpp`): `HRT1`, BE
  width/height, RGBA rows. Produced by the Lab, consumed here.
- **`blit_scaled`** — nearest-neighbour resample of a `Sprite` into a destination rect.
- **texture name** — the asset base name (`studio_NN`) stored on a `Sprite`; the reference
  the pure core carries instead of pixels.
- **collection probe** — the fixed-range name loop that discovers saved textures without a
  directory listing.

## Exercises

1. **Rotated blit.** Actors carry `rot` but the texture ignores it. Add a
   `blit_rotated_scaled` that samples the source through the inverse rotation. Where does
   the extra cost show up, and is nearest-neighbour still good enough?
2. **Tint.** Add a `tint` colour multiplied into each texel in `blit_scaled`, so one
   grayscale texture yields many coloured actors. Which layer holds the tint — core or
   scene?
3. **Manifest discovery.** Replace the 32-name probe with a `textures/manifest.txt` the Lab
   appends to on Save. What breaks in the web build, and what doesn't?
4. **Round textures.** A textured `round` actor still blits a square. Mask the blit to a
   circle (skip texels outside `dw/2`). Is that a `blit_scaled` option or a new primitive?
