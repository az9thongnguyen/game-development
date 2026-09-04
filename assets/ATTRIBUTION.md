# Attribution

Everything in this repository is hand-written by its author **except** the art listed
here, which comes from open-licence packs.

The rule this file exists to keep: **art enters through `asset.import` and is recorded
here in the same change**. A pack that lands in `assets/` without a line below is a
licence obligation nobody can see, and CC-BY packs make that obligation real.

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

## Fonts

| | |
|---|---|
| **Inter**, **JetBrains Mono** | `assets/fonts/` — SIL Open Font License 1.1, licence text alongside the files |

## Everything else

Every other pixel in `assets/` was produced by code in this repository — the Texture
Lab (`--lab texture`), the sprite generators in the demos, or the Map/Scene
workspaces. Art drawn there and art imported from a pack are the same format on disk
(`.hrt`), which is what lets one be swapped for the other a tile at a time.
