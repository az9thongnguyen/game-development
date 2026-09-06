# Farm

A small farm you can play in a browser or on your desktop — the same build, the same
save. Written on a hand-made engine: every pixel on screen is drawn by this project's
own code into a CPU framebuffer, with SDL2 doing nothing but hand over a window.

## Features

- A day loop: plant, water, wait, harvest, sell, sleep — crops grow between days.
- Four tools on a hotbar, and every one of them reachable with a finger.
- An NPC with a schedule and a branching conversation you answer by tapping.
- Save files that are plain text, and a cloud save that tells you when it disagrees
  with the local one instead of quietly picking a winner.
- Prices that come from remote config, so a live event can change them without a
  new build.
- Autotiled paths — sixteen pieces chosen from a cell's four neighbours.

## Controls

| Key | Touch | Command |
|---|---|---|
| Arrows / WASD | d-pad | walk |
| Z / Space / Enter | **use** | use the held tool, talk, answer |
| Q | **seed** | cycle the seed in hand |
| 1 2 3 4 | hotbar slots | hoe · water · seed · harvest |
| X / Esc | tap outside | leave a conversation |
| F5 | **save** | save, and push to the cloud |
| F9 | — | reload the last save |
| F6 / F7 | the two buttons that replace **save** during a conflict | keep mine / take the cloud's |

Every verb has an on-screen control, and both the drawing and the hit test read one
`farm::layout()` — a button drawn in one place and pressed in another is invisible in
a screenshot, which is how the last one was found.

## Instructions

1. **Add a crop** — edit `assets/farm/crops.def` (name, days per stage, buy/sell price).
   No rebuild; the file is read at launch.
2. **Add a map** — open the Map workspace (`./build/demo --lab map`, or the Studio's
   Edit section) and save over `assets/maps/farm_home.map2`.
3. **Change the art** — `assets/farm/theme.def` names the sheets. Drawn art is a `.pix`
   ASCII sheet re-baked with `--cmd asset.pixels`; imported art comes in through
   `--cmd asset.import`. Either way it gains a line in `assets/ATTRIBUTION.md`.
4. **Saves** — `assets/saves/farm/slot1.sav`, plain text. On the web it lives in the
   browser's IndexedDB, mounted at the same path.
5. **Publish** — `./build/demo --project-publish projects/farm.gameproject development
   "why"`, then promote through `preview` and `production`.

## Run it

```sh
./build/demo --project projects/farm.gameproject      # native
# or open the web build:  demo.html?project=projects/farm.gameproject
```

## License

Code: this repository's licence. Art: see `assets/ATTRIBUTION.md` — the tileset is
Kenney's Tiny Town (CC0); the path pieces were drawn here.

## Chapters

The guidebook chapter for each part: `docs/book/124`–`126` (the farm and its controls),
`128` (the page it ships on), `130` (this collection page).
