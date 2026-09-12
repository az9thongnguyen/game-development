# 150 — A front door, not a directory

The Collection page already did the difficult parts. It discovered projects from one
baked index, decoded the engine's `.hrt` format without a PNG export path, kept broken
projects visible, fitted a phone and followed Play all the way into a running WASM game.
It still read like a diagnostic directory: a small heading, five nearly identical cards,
manifest-shaped metadata and a player bar that printed the manifest path back at the
person who chose it.

The difference between a directory and a front door is not decoration. A stranger must
understand what this is, see the choices as different products, know which action is
primary and retain a route back after choosing one.

## A contract the browser can observe

The redesign started with assertions, not CSS. At 390×844 the page must have a product
hero and value proposition, report the number of playable experiences, use semantic
articles with an eyebrow/name hierarchy, expose five distinct dedicated covers, keep
Play at least 44 pixels high and never scroll sideways. Every Play link needs the game's
name, not merely the word “Play”. Details must expose both collapsed and expanded state.

The player has its own contract: a route back to `collection.html`, the human project
name rather than `projects/foo.gameproject`, a runtime status with a machine-readable
state, named 44-pixel controls and no horizontal overflow. The Log control is tested in
both directions: its panel and `aria-pressed` state must appear together and disappear
together.

That contract produced the page now on screen: a product-level hero, two-column desktop
grid, one-column phone grid, per-game hierarchy, one primary CTA and the same navy /
periwinkle vocabulary chapter 149 put into the native UI. A reduced-motion media query
removes the lift and arrow transitions without changing layout.

## Five cards, five identities, four doors

Iso, Colony and Creatures already had dedicated cover files. Creator reused a wall
texture; Farm reused the whole Tiny Town sheet. Both loaded correctly and both said
“asset browser” rather than “game”. `creator_cover.pix` draws the raycaster's neon
corridor language. `farm_cover.pix` draws a sunrise, barn, field rows and pond. Each is
32×32 source text baked by the existing `asset.pixels` command and byte-checked by the
tree-wide source sweep.

No screenshot-to-texture shortcut was added. `.hrt` still has exactly four offline
doors, and provenance still derives from their marks. The regenerated ledger sees 51
raster assets and zero unrecorded ones.

The first manifests declared each cover twice: once as `cover`, again as
`asset texture`. Inspection de-duplicated the resource closure, so packaging looked
correct. The Studio did not: Pixels lists declared editable textures, so Creator's cover
became another inspector row and pushed Save off the bottom at 1280×720. The first full
CTest run caught both consequences: Farm's closure count had honestly moved from 11 to
12, and the Pixels inspector no longer fit.

The fix was deletion. `cover` already joins the closure as type `cover`; it does not need
to pretend it is an editable gameplay texture. The manifests now match Iso and Colony,
the closure still contains the cover, Farm's count is pinned at 12 and the Pixels panel
fits again.

## One product name, not another table

The first player shell mapped five manifest stems to five labels in JavaScript. That
would become stale on game six — the same second-list failure `collection.index` exists
to prevent. The shell now reads the selected manifest's name from `collection.json`,
which is itself baked from the manifests. A readable title-cased stem is only an offline
fallback; a missing index never blocks the game.

Review also removed two claims the product could not honestly make. “Persistent saves”
was false for Iso while it still writes outside the IDBFS-mounted `saves/` directory, and
“touch” described only some game controls. The page now says what every card can support:
keyboard + pointer, native + WebAssembly.

Loading and running are no longer the same green pill. Runtime status carries a state;
only `running` gets the success treatment. Log and Fullscreen carry initial and live
`aria-pressed` values, names and focus rings rather than relying on their glyphs.

## Mutation and the survivor that was still 47 pixels

Twenty-one mutations exercised the hero, headline, promise, phone grid, semantic cards,
cover identity, accessible names, playable count, Details state, back route, player
identity, status, control size, overflow and Log state. Twenty were killed. Changing
Play's `min-height` from 48 to 28 survived because its text line, vertical padding and
border still produced a roughly 47-pixel target. The observable guarantee remained true;
this is an equivalent mutation, not an untested branch. It is recorded rather than
converted into a test for the implementation detail `min-height`.

## What is verified, and what is not

Verified:

- The native build completed and `test_shell_golden` passed after the duplicate cover
  declarations were removed.
- **95/95 CTest passed twice** after the fix: 36.08 seconds, then 62.98 seconds.
- Emscripten 3.1.61 built `demo.html` from the final sources.
- Chrome at 390×844 decoded all five covers, found five playable responsive cards and
  50-pixel Play targets, rendered the README, tapped Play into a running Colony, read
  the manifest-derived name, exercised both Log states and found no horizontal scroll.
- The Collection and player screenshots were inspected; no cover resampling, overlap or
  clipping was observed.
- 21 mutations were attempted: 20 killed, one equivalent size survivor triaged above.
- Release render benchmark, 200 frames: Studio ss1 0.94 ms / ss2 6.99 ms, FPS 1.14 ms,
  Farm 2.00 ms, Creatures 3.17 ms, Iso 3.09 ms and Colony 6.48 ms median. Every median
  stayed below the 8 ms budget; p95 remains noisy and exceeded it for Studio ss2 and
  Colony.
- Farm inspected, published and verified release `150f60129cb57b22`, promoted
  development to preview, reached a shippable Hub and left zero `.tmp` files.

Not verified yet:

- No physical phone or screen reader was used; Chrome touch emulation and DOM state are
  the current accessibility evidence.
- Fullscreen state wiring is implemented but was not entered by the automated journey;
  browsers gate fullscreen on a trusted user gesture.
- The journey follows the first card, Colony. All five covers and links are inspected,
  but the other four games were not launched in this browser run.
- Iso's save path and Colony's runtime-generated `.hrt` remain real debts for S37; this
  chapter stopped advertising around the first and did not hide the second.
