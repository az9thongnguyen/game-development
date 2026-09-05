# Attribution

Everything in this repository is hand-written by its author **except** the art listed
here, which comes from open-licence packs.

The rule this file exists to keep: **art enters through `asset.import` and is recorded
here in the same change**. A pack that lands in `assets/` without a line below is a
licence obligation nobody can see, and CC-BY packs make that obligation real.

Art this project MADE enters through the other door, `asset.texture`, and is listed
below too. It carries no obligation — the point of writing it down is the opposite
one: a file that is ours should be provably ours, and a sheet that mixes the two
sources would be neither. That is why the pond is a separate `.hrt` rather than a
pixel painted into Kenney's.

## Kenney — Tiny Town 1.1

| | |
|---|---|
| **Source** | <https://kenney.nl/assets/tiny-town> |
| **Author** | Kenney (<https://kenney.nl>) |
| **Licence** | [CC0 1.0 Universal](http://creativecommons.org/publicdomain/zero/1.0/) (public domain) |
| **Downloaded** | 2026-09-04, `kenney_tiny-town.zip` |
| **In this repo** | `assets/textures/kenney_tiny_town.png` — the pack's `Tilemap/tilemap_packed.png`, 192×176, a 12 × 11 grid of 16 px tiles |
| **Derived** | `assets/textures/town.hrt` — the same image, imported with `demo --cmd asset.import` |

CC0 requires no credit. Kenney is credited anyway, and the source PNG is committed
next to the imported `.hrt`, so the import can be re-run and checked rather than
taken on trust.

Reproduce it with:

```sh
./build/demo --cmd asset.import textures/kenney_tiny_town.png textures/town.hrt
```

## Ours — the farm pond

| | |
|---|---|
| **Source** | this repository, the Studio's Texture Lab (`--lab texture`) |
| **Licence** | same as the rest of the repository |
| **In this repo** | `assets/textures/farm_water.recipe` — the twelve generator parameters, plus why they are those |
| **Derived** | `assets/textures/farm_water.hrt` — 16×16, one tile, two shades of blue |

Tiny Town has no water tile, so the farm's pond stayed a flat rectangle for a chapter
while everything around it had art. This is that tile. It is generated, not painted:
one octave of value-basis noise thresholded high, which leaves most of the tile flat
and lifts about an eighth of it to a lighter blue — short ripple dashes that sit next
to Kenney's flat-colour tiles instead of fighting them.

The `.recipe` is not a comment. `test_farm` regenerates the `.hrt` from it and compares
bytes, so "drawn in the Studio" is a checked fact and the tile can be re-edited rather
than only re-admired.

Reproduce it with:

```sh
./build/demo --cmd asset.texture textures/farm_water.recipe textures/farm_water.hrt
```

## Fonts

| | |
|---|---|
| **Inter**, **JetBrains Mono** | `assets/fonts/` — SIL Open Font License 1.1, licence text alongside the files |

## Everything else

Every other pixel in `assets/` was produced by code in this repository — the Texture
Lab (`--lab texture`), the sprite generators in the demos, or the Map/Scene
workspaces. Art drawn there and art imported from a pack are the same format on disk
(`.hrt`), which is what lets one be swapped for the other a tile at a time — and
`assets/farm/theme.def` is the proof: two sheets with two origins, joined per tile id,
and neither file knows the other exists.
