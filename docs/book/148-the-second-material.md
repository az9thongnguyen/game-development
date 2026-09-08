# 148 — The second material

The map had two autotile rules and one piece of art that used either of them. The farm
path proved `line`; `blob` could be authored and previewed, but no runtime content ever
asked it for one of its 47 pieces. At the same time the Texture Lab could export a
vertical animation sheet, while both tilemap games cut every row into unrelated static
tiles.

Those are the same missing joint: a map material needs to select both the piece for its
neighbours and the frame for time.

## Forty-seven pieces, not forty-seven guesses

Creatures' long grass is now `rule 3 blob` in the map. Its source,
`textures/creature_grass.pix`, follows the stable order produced by
`autotile_index`: masks are enumerated from 0 to 255 and the first appearance of each
canonical shape becomes the next index. The result is exactly 47 tiles. Connected
cardinals extend the darker grass through an edge; a diagonal fills a corner only after
canonicalisation says both adjacent cardinals exist.

The sheet keeps plain grass opaque beneath the darker patch. A transparent edge would
not reveal “the tile below” — there is no tile below on that layer — it would reveal the
framebuffer clear colour. `asset.pixels` bakes the 752×16 `.hrt`, and the command test
re-bakes it byte-for-byte.

## Time belongs beside the cut sheet

`AnimatedTileset` wraps the existing `Tileset`. A normal atlas preserves its old count
and indices. A taller, exact stack of square frames uses `anim::frames_in_sheet`; callers
see the tiles in one frame, and `sprite(index)` adds the current frame offset. Farm and
Creatures both own the wrapper and advance it from the same `update(dt)` seam.

This first runtime contract intentionally supports the shape the Texture Lab already
exports: vertically stacked square frames. It does not add animation fields to maps or
themes, and it does not make a fifth asset door.

The recipe gained a separate `TextureRecipe { texture, frames }` rather than putting a
runtime concern into `TextureParams`. The old twelve-key `to_recipe(TextureParams)` and
`from_recipe` remain byte-compatible; `asset.texture` reads the complete recipe and calls
the same `make_sheet` the Lab uses. `frames` is clamped to 1…64. The farm water recipe now
asks for four frames and bakes to 16×64, so both games animate the same asset without
duplicating timing code.

## The survivor that crossed into another frame

Nineteen single-token mutations ran. One survived first: changing the animated range
check from `>=` to `>` still passed. It looked redundant because the wrapped `Tileset`
also rejects an out-of-range index. That was only true on frame zero of a static atlas.
On an animated sheet, index one plus the frame offset points at a real tile in the next
frame — a valid sprite with the wrong meaning. Tests now ask out-of-range in two frames;
the second run killed all 19 mutations.

## What is verified, and what is not

Verified:

- `test_studio`, `test_commands`, `test_tilemap`, `test_farm`, `test_farm_scene` and
  `test_creatures_scene` pass with the new assets.
- The water source re-bakes to 16×64 and the grass source to 752×16; both committed
  `.hrt` files compare byte-for-byte with a fresh bake.
- Farm and Creatures advance the water frame and lift out-of-range indices in both
  directions; static atlases remain static.
- **19 mutations, 19 killed.**
- Rendered Farm and Creatures frames were inspected. Water has a coherent ripple frame;
  long-grass edges join without transparent holes.
- **95/95 `ctest`, twice** (37.97 s and 27.36 s after the derived ledgers were rebaked).
- Emscripten 3.1.61 configured and linked `demo.html`; doing so found that CMake required
  pthreads for a job system that is synchronous on web, and the unused requirement is gone.
- The Creatures golden path inspected, published release `41369a0bce39b6bb`, verified
  parity, promoted development to preview, and reached a shippable Hub with zero `.tmp`.
- Release render benchmark, 200 frames: Farm 0.80 ms median, Creatures 1.21 ms; all seven
  configurations stayed under the 8 ms budget.

Not verified yet:

- Animation speed is one shared 4 fps default. There is no per-material timing metadata.
- A frame may contain multiple tiles mathematically, but no animated autotile set is
  authored; only the one-tile water stack exercises animation at runtime.
