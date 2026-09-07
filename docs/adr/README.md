# Architecture decisions

An **index**, not an archive. Every decision below is already explained, in full, in a
chapter of `docs/book/` — this file exists so that "why is it like that?" has one place
to start, and so that a decision that was later **replaced** says so out loud instead of
surviving as folklore.

There is no prose here on purpose. One line names the decision; the chapter is where the
argument lives. If a row and its chapter disagree, the chapter is right and the row is a
bug.

`test_adr_index` checks that every chapter this file points at exists, that no id is
used twice, and that every `Superseded by` target is itself a row — the three ways an
index like this rots.

**Status** is one of:

- **Accepted** — still how the code works.
- **Superseded by N** — replaced; the row stays, because a decision that was reversed is
  more useful to a reader than one that was quietly deleted.

---

## Platform and engine

| # | Decision | Chapter | Status |
|---|---|---|---|
| 1 | SDL2 is the ONLY runtime dependency, and only as a thin shim behind `platform.hpp` | [02](../book/02-platform-layer.md) | Accepted |
| 2 | A frame is one `App::frame(dt)`; no blocking `while(true)` above the platform layer | [03](../book/03-game-loop-and-timestep.md) | Accepted |
| 3 | Logic runs on a fixed timestep with one spiral-of-death clamp, shared with the Studio's Play viewport | [03](../book/03-game-loop-and-timestep.md), [115](../book/115-play-viewport.md) | Accepted |
| 4 | Every pixel is drawn by our own code into a CPU framebuffer; no SDL drawing primitives | [05](../book/05-software-renderer-2d.md) | Accepted |
| 5 | All file I/O goes through `assets::`, never a scattered `fopen` | [07](../book/07-assets.md) | Accepted |
| 6 | CMake source lists are written out; no globbing | [01](../book/01-build-and-toolchain.md) | Accepted |
| 7 | The same engine and game code compiles native and WASM; only `run()` is `#ifdef`'d | [32](../book/32-web-port-wasm.md) | Accepted |
| 8 | The desktop target can be switched off, so a server image builds with no SDL2 present | [107](../book/107-backend-container-and-build-split.md) | Accepted |
| 9 | The operation lives in a pure `*_core` library; the trigger — a CLI flag, a keypress, a button — only calls it | [111](../book/111-commands-and-undo.md) | Accepted |

## Content formats

| # | Decision | Chapter | Status |
|---|---|---|---|
| 20 | `.hrt` (`HRT1\|w\|h\|RGBA8`) is the only raster format the engine reads at runtime | [77](../book/77-textured-sprites.md) | Accepted |
| 21 | `.hrt` has three offline doors, one per origin: import, texture, pixels | [125](../book/125-the-pieces-the-pack-did-not-have.md) | Superseded by 22 |
| 22 | A fourth door, `asset.mix`, because it answers a question the other three cannot: *a cast, not a picture* | [135](../book/135-a-cast-not-a-drawing.md) | Accepted |
| 23 | Every new `.hrt` gains a hand-written line in `ATTRIBUTION.md` in the same change | [122](../book/122-the-tile-the-pack-did-not-have.md) | Superseded by 24 |
| 24 | Provenance is DERIVED from the marks the doors leave on disk; the ledger generates itself | [131](../book/131-a-rule-a-machine-keeps.md) | Accepted |
| 25 | `fpsmap1` is the map format, and Map Lab is where a map is edited | [110](../book/110-one-map-format.md) | Superseded by 26 |
| 26 | `map2` is the only format anything WRITES; the old reader survives with no writer at all | [132](../book/132-one-map-format.md) | Accepted |
| 27 | `map2` is written at the LOWEST version that can express the file, so a release id does not move for a feature the file does not use | [134](../book/134-a-road-is-a-road.md) | Accepted |
| 28 | Whether a material is a road or a region lives in the MAP, not in a game's art file | [134](../book/134-a-road-is-a-road.md) | Accepted |

## The platform spine

| # | Decision | Chapter | Status |
|---|---|---|---|
| 40 | A game is launched from a versioned manifest, not a CLI flag | [90](../book/90-project-manifest-and-golden-path.md) | Accepted |
| 41 | Manifest entries live in ONE table, so a game cannot be launchable-but-unknown | [116](../book/116-two-workspaces.md) | Accepted |
| 42 | `engine::inspect()` is the one read + validate + hash, and returns DATA, never printed lines | [91](../book/91-resource-identity-and-closure.md), [114](../book/114-project-workspace-and-audit.md) | Accepted |
| 43 | The release id IS the package hash: order-independent, content-sensitive | [92](../book/92-package-manifest.md) | Accepted |
| 44 | `releases/<hash>/` is immutable and content-addressed; channels are pointers | [93](../book/93-immutable-release-store.md) | Accepted |
| 45 | A publish is atomic and audited; a blank reason is refused | [94](../book/94-atomic-audited-releases.md) | Accepted |
| 46 | One pure `hub_lines`/`recommend`, shared by the headless CLI and the window | [95](../book/95-hub-shell-and-recommended-action.md) | Accepted |
| 47 | Every operation registers once under a stable id; `--cmd`, `Cmd+K` and a button all go through `cmd::run` | [114](../book/114-project-workspace-and-audit.md) | Accepted |
| 48 | Twelve one-per-scene CLI flags for the labs | [78](../book/78-map-level-lab.md) | Superseded by 49 |
| 49 | One door per kind: a game from its manifest, the Studio behind one flag, every lab behind `--lab` | [120](../book/120-one-door-per-kind.md) | Accepted |

## Determinism

| # | Decision | Chapter | Status |
|---|---|---|---|
| 60 | The engine ships its own RNG (xorshift64\*), because `std::mt19937` is portable and its distributions are not | [136](../book/136-a-battle-that-replays.md) | Accepted |
| 61 | A number two machines must agree about does not get to be a float | [136](../book/136-a-battle-that-replays.md) | Accepted |
| 62 | The battle RNG is a hashed FIELD of the state, and turn order is derived from state alone | [136](../book/136-a-battle-that-replays.md) | Accepted |
| 63 | A replay is a FILE with the start state and a hash after EVERY turn, so a divergence is reported where it happened | [138](../book/138-a-fact-you-can-hand-to-someone-else.md) | Accepted |
| 64 | The rules fingerprint covers what `step` reads and nothing else, so moving a creature to another patch of grass does not invalidate a recording | [138](../book/138-a-fact-you-can-hand-to-someone-else.md) | Accepted |
| 65 | Integer Elo lives in ONE header on both the client's and the server's include path — the single deliberate exception to "the backend links no engine code" | [139](../book/139-four-bytes-a-turn.md) | Accepted |

## The backend

| # | Decision | Chapter | Status |
|---|---|---|---|
| 80 | The backend is a separate process and links none of the engine or game code | [51](../book/51-baas-overview-and-architecture.md) | Accepted (one exception: 65) |
| 81 | A read-then-write is atomic because the SQLite pool is one connection | [103](../book/103-atomic-purchase.md) | Superseded by 82 |
| 82 | Every read-then-write is a transaction with a locking read; `db::lock_clause()` is where the lock is asked for | [140](../book/140-the-pool-was-the-lock.md) | Accepted |
| 83 | …and a MATERIALISED row first, because `FOR UPDATE` cannot hold a row that does not exist | [141](../book/141-four-runs-of-one-script.md) | Accepted |
| 84 | Postgres is a documented deploy-time build | [54](../book/54-persistence-and-data-model.md) | Superseded by 85 |
| 85 | One dialect in the source and two on the wire: `db::portable` translates, `db::exec` is the only door, and a source scan proves nothing goes around it | [141](../book/141-four-runs-of-one-script.md) | Accepted |
| 86 | A transaction waits for its COMMIT before returning, so a POST cannot answer before the GET after it can see it | [141](../book/141-four-runs-of-one-script.md) | Accepted |
| 87 | The API describes itself from the same table it routes from, and a test compares the document to Drogon's own route list | [142](../book/142-the-image-had-never-answered.md) | Accepted |
| 88 | `--seed` creates the demo project and exits | [54](../book/54-persistence-and-data-model.md) | Superseded by 89 |
| 89 | `--seed` seeds and then SERVES; `--seed-only` is the one-shot | [142](../book/142-the-image-had-never-answered.md) | Accepted |

## Games and interface

| # | Decision | Chapter | Status |
|---|---|---|---|
| 100 | The parts of an on-screen control that are facts about a HAND are shared; the LAYOUTS are not | [124](../book/124-playable-by-hand.md), [137](../book/137-a-second-game.md) | Accepted |
| 101 | ONE layout function that the renderer and the hit test both read — a control drawn in one place and hit in another is invisible in a screenshot | [126](../book/126-the-verbs-a-thumb-could-not-reach.md) | Accepted |
| 102 | Every verb has an on-screen control, not only a key | [124](../book/124-playable-by-hand.md) | Accepted |
| 103 | A crop's `season` is a label stored with the definition | [113](../book/113-farm-vertical-slice.md) | Superseded by 104 |
| 104 | `season` is a RULE, enforced at planting and at the season boundary through one `grows_in`, and readable on the HUD | [143](../book/143-a-field-nobody-read.md) | Accepted |
| 105 | The Studio's canvas/inspector split is FIXED — a draggable one costs a cursor shape, a hit zone and a persisted position | [112](../book/112-map-workspace-and-palette.md) | Superseded by 106 |
| 106 | The split is draggable and per workspace; the STORED width is never clamped and the DRAWN width always is, so a window you shrank cannot take a layout away | [144](../book/144-the-comment-that-named-its-own-slice.md) | Accepted |
| 107 | A workspace's status strip is a SENTENCE it builds itself | [116](../book/116-two-workspaces.md) | Superseded by 108 |
| 108 | The status strip is a list of cells, each with its own tone; the separator is drawn, not typed | [144](../book/144-the-comment-that-named-its-own-slice.md) | Accepted |
| 109 | A Scene REPORTS a `ui::CursorHint` and `App` applies it — a scene cannot call `platform::set_cursor`, because most of them compile into tests that link no backend | [144](../book/144-the-comment-that-named-its-own-slice.md) | Accepted |
| 110 | The package hash — the release id — is a fingerprint of the RESOURCES | [93](../book/93-immutable-release-store.md) | Superseded by 111 |
| 111 | The release id fingerprints the whole package FILE, identity included: two projects that ship nothing, or share their art and differ only in `entry`, are two releases | [145](../book/145-two-games-that-hashed-the-same.md) | Accepted |
| 112 | A lab is a demo of a subsystem or a scene with no manifest; `iso` and `colony` were games sitting in that table and now have manifests | [145](../book/145-two-games-that-hashed-the-same.md) | Accepted |
| 113 | A benchmark lives in a core like every other operation, so its arithmetic can be asked a question without allocating a framebuffer | [145](../book/145-two-games-that-hashed-the-same.md) | Accepted |
| 114 | The rated-match client picks its own actions with `choose` | [139](../book/139-four-bytes-a-turn.md) | Superseded by 115 |
| 115 | Who picks the action is a SWITCH on the client (`set_auto_play`), so the headless worker and the game screen are one implementation | [146](../book/146-a-match-nobody-could-play.md) | Accepted |
| 116 | ONE `shown_battle()`/`my_side()` decides which battle is on screen and which half is yours — the server hands out sides, and a screen hard-coded to 0 shows you your opponent's party | [146](../book/146-a-match-nobody-could-play.md) | Accepted |
