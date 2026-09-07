# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A **hand-written C++20 game engine** built from scratch to learn deeply, plus a
collection of games/tools on top of it and a hand-written Game Backend-as-a-Service.
`requirements.md` (Vietnamese) owns the original *learning* vision; `docs/strategy/`
owns the *product* direction (how this grows into a self-hostable game-creation
platform) — read it before touching the platform spine. `docs/book/` is a 100+-chapter
guidebook where each chapter maps to the code that implements it; **the guidebook
chapter is the best explanation of any given subsystem** (e.g. `93` = release store,
`95`–`97` = Hub/Studio shell). `docs/guides/author-to-url.md` walks the operator
golden path end to end. **`docs/PROJECT-BRIEF.md` is the single orientation document**:
current state, full feature inventory, a verified-vs-unproven ledger, the roadmap
position, and the decision rules for choosing what to build next — read it before
picking work.

## Non-negotiable architectural constraints

These are the rules the whole design exists to protect. Breaking them is a design
regression, not a shortcut:

- **SDL2 is the ONLY runtime dependency, and only as a thin shim** — window, raw
  framebuffer present, raw input, audio, timing. **Never** use SDL drawing
  primitives (`SDL_Renderer`, `SDL_image`, …). Every pixel is drawn by our own code
  into a CPU framebuffer.
- **Engine/game code never `#include <SDL.h>`.** It talks only to
  `src/platform/platform.hpp` — the fixed platform seam. SDL lives behind it in
  `backend_sdl.cpp`. If an SDL type wants to appear in a header above the platform
  layer, the abstraction is leaking.
- **No blocking `while(true)` game loop above the platform layer.** A frame is one
  `App::frame(dt)` call driven by `platform::run()`. This is what lets the web build
  swap in `emscripten_set_main_loop` with zero changes to engine/game code.
- **All file I/O goes through `assets::` (`src/engine/assets.*`)**, never scattered
  `fopen`. The web build uses a virtual filesystem.
- **`map2` is the only map format anything WRITES** (chapter 132). The older
  `fpsmap1` has exactly one remaining reader — `tilemap::from_fpsmap1`, reached
  through `tilemap::load` — and no writer at all: Map Lab, its only producer, was
  retired along with `fps::to_text`/`from_text`. Convert an old file with
  `--cmd map.migrate <src.map> <dst.map2>`. A format nothing can write cannot come
  back, which is the whole point of deleting the writer rather than deprecating it.
  **`map2` is written at the LOWEST version that can express the file** (chapter 134):
  v2 added per-layer autotile `rule` lines, so a map with no rules is still written as
  `map2 1` and its bytes — and its release id — do not move for a feature it does not
  use. The version guard is what makes that safe: a file from the future is refused,
  never half-read.
- **Whether a material is a road or a region lives in the MAP, not in a game's art
  file** (chapter 134). `rule <value> line|blob` under a layer; `tilemap::rule_piece`
  is the ONE neighbour scan (it answers 0 for an unruled value, so a renderer writes
  `base + rule_piece(...)` with no branch). `farm::line_piece` and the theme's
  `autotile` record are gone — a copy of that decision inside one game is why the Map
  workspace drew a flat square for a road it had no idea was a road.
- **`.hrt` is the only raster format the engine READS at runtime** (`HRT1|w|h|RGBA8`).
  It has exactly **four offline doors**, one per origin, so art from a pack and art
  this project made arrive downstream as the same kind of file. **The count went from
  three to four in chapter 135, deliberately** — a fourth door is a decision, not a
  side effect, and the bar it had to clear was: it answers a question none of the
  other three can. It does. The first three all answer *where did this picture come
  from*; `asset.mix` answers **make me a hundred sprites that all belong together**,
  which is the difference between a tool for one picture and a tool for a cast:
  `--cmd asset.import` (a PNG, decoded by hand via `inflate_core` + `png_core`, no third
  party), `--cmd asset.texture` (the Texture Lab's `.recipe` — art we *generated*), and
  `--cmd asset.pixels` (a `.pix` ASCII sheet — art we *drew*; text because a tile SET is
  one design cut N ways and its seams are a relationship you review, not sixteen
  canvases you click through), and `--cmd asset.mix` (a `.mix` — art *assembled* from
  parts of other `.hrt` files, with palette swaps; the farm's player and Anna are the
  same body and head, and Anna is a hat plus two colour swaps). A `.recipe`/`.pix`/
  `.mix` is a *source*: it stays out of the manifest, and a test re-bakes it and
  compares bytes — as does every `import` line in every `.pack`, so all four doors are
  held to one standard.
- **Provenance is DERIVED, not remembered** (`provenance_core`, chapter 131). The rule
  used to read "every new `.hrt` gains a line in `assets/ATTRIBUTION.md` in the same
  change", and by the time it was checked it had been forgotten twenty times out of
  twenty-three. The four doors leave four different marks on disk — a `.pack` naming
  the import, a sibling `.recipe`, a sibling `.pix`, a sibling `.mix` — so
  `engine::scan_provenance()` reads them instead. A new door costs a new mark and a
  new origin, and that cost is part of what a fourth door has to be worth. Anything else is `UNRECORDED` and `test_provenance` goes RED.
  The ledger table inside `ATTRIBUTION.md` is **generated** between two markers (the
  prose around it is hand-written and survives); re-bake it with
  `--cmd asset.attribution ATTRIBUTION.md`. Art with no surviving source is `declared`
  — named one by one in a `.pack`, which is a weaker claim and reads as one.
  **What you still owe by hand is the PROSE**: why the tile exists, whose palette it
  borrows. The list keeps itself.
- **Web-portability is baked in from the start.** The same engine/game code compiles
  native and WASM; only the platform `run()` loop is `#ifdef`'d.
- **A number two machines must agree about does not get to be a float** — and if the
  two machines are the CLIENT and the SERVER, they share the header rather than each
  keeping a copy. `engine/elo.hpp` (integer Elo: a per-mille table, interpolated in
  integers, mirrored so `E(d)+E(-d)==1000` exactly, rounded away from zero so a match
  is zero-sum) is on `baas_core`'s include path. That is the ONE deliberate exception
  to "the backend links no engine code" (chapter 139): nothing is linked, one
  header-only `constexpr` file is shared, and the reason is exactly why the table
  exists — a rating computed from two copies of a curve is the bug it prevents.

## Build, run, test

```sh
brew install cmake sdl2                        # prerequisites (macOS)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Everything is one `demo` executable; the first arg picks the mode. **`demo --help`
prints every mode this build answers to** (and where the retired flags went — an
unknown `--flag` is an error with that list, never a silently different window).

The surface is deliberately small: a **game** is launched from its manifest, the
**Studio** is one flag, and everything else windowed is a **lab** behind one door.
Twelve one-per-scene flags used to sit here; chapter 120 folded them.

```sh
./build/demo            # M0 engine demo (retro 480x270)
./build/demo --gui [hvh|hvai] [easy|medium|hard]   # chess GUI      (--tui = terminal)
./build/demo --fps      # M2 raycaster (loads maps/level_00.map2)

./build/demo --lab      # list the labs; --lab <id> runs one
#   scene    the Studio's Scene workspace, full-screen (the SAME object as its Scene tab):
#            place/drag actors, and the EFFECTS section on the selected one — an
#            Emitter (particles), a Light that rides the actor, a Sound heard when it
#            is destroyed, and a Flipbook for an animated sheet. Chapter 133 folded the
#            four effect labs into these; the cores they demoed all stayed.
#   map      the Studio's Map workspace, full-screen (the SAME object as its Map tab):
#            paint/rect/fill on layers, the Entity tool that places a spawn, and the
#            Rule button — none/line/blob for the brush's material. A ruled cell draws
#            its CONNECTIONS (there is no tileset renderer here), which is the only
#            thing on this canvas that says a road is a road
#   pixel    the Studio's Pixels workspace, full-screen (pencil/rect/fill/pick on .hrt,
#            palette sampled from the image + an HSV/hex mixer for a colour it lacks)
#   mixer    the Studio's Mixer workspace, full-screen (the SAME object as its Mixer
#            tab): a sprite ASSEMBLED from parts of a sheet, plus palette swaps.
#            Save writes the .mix (the source); Bake writes the .hrt
#   texture  Texture Lab: procedural noise -> .hrt + re-editable .recipe, sheet export
#   editor   immediate-mode GUI + physics sandbox
#   3d viz3d                software-rasterized 3D core / interactive sandbox
#   iso      M4 isometric farm sim (F5/F9 save/load)
#   colony   engine-core integration game (also the BaaS/SDK client)

./build/demo --shell [proj]     # the Studio (1280x720, resizable)
                                # Edit section: TABS of workspaces (Map | Scene | Pixels | Mixer), Cmd+Z undo,
                                # Cmd+S save, Cmd+K palette, Cmd+1..7 sections. Autosaves;
                                # offers recovery on open.
                                # Project section = asset browser + validation verdict (the
                                # same engine::inspect --project-inspect prints).
                                # Play section RUNS the project's entry scene in a framebuffer
                                # of its own — Pause, Step one frame, the mouse and keyboard
                                # reach the game, Esc returns them; Hub shows the audit log.
```

**Headless platform-spine verbs** (no window; these are what CI smoke-tests, so keep
them working). Paths are relative to the asset root — see `assets::` below:

```sh
./build/demo --cmd asset.import  <src.png>    <dst.hrt>  # bring foreign art in (offline)
./build/demo --cmd asset.texture <src.recipe> <dst.hrt>  # bake GENERATED art (offline)
./build/demo --cmd asset.pixels  <src.pix>    <dst.hrt>  # bake DRAWN art, an ASCII sheet
./build/demo --cmd asset.mix     <src.mix>    <dst.hrt>  # bake ASSEMBLED art: parts + swaps
./build/demo --cmd asset.new <name> <tile-px> <cols> <rows> [<proj>]  # a NEW sheet:
                                              # .pix source -> .hrt -> declared in the
                                              # manifest -> ledger re-baked, in one act
./build/demo --cmd asset.attribution ATTRIBUTION.md   # re-bake the provenance ledger
                                              # (exits non-zero on an UNRECORDED asset)
./build/demo --cmd collection.index projects collection.json  # bake the game LIST a page reads
                                              # (assets/collection.json is committed; a test
                                              #  re-bakes it and compares bytes, like a .recipe)
./build/demo --cmd creature.verify creatures/reference.crep   # re-play a RECORDED battle and
                                              # check the hash after every turn (ch.138). Exit 1 on
                                              # DESYNC (two machines disagree about the integer
                                              # arithmetic) or on RULES MOVED (somebody re-tuned a
                                              # move) — reported apart, because they are different
                                              # jobs. assets/creatures/reference.crep is COMMITTED
                                              # and a test re-bakes it and compares BYTES, so CI on
                                              # linux/x86_64/gcc checks a file macOS/arm64/clang
                                              # wrote. That byte comparison is the only cross-ISA
                                              # determinism proof in the repo; a thousand replays
                                              # inside one process is a much weaker claim
./build/demo --cmd creature.record creatures/reference.crep   # ...re-bake it
./build/demo --project-new projects/mine.gameproject fps "My Game"   # create
./build/demo --project projects/creator.gameproject                  # launch from manifest
./build/demo --project projects/creatures.gameproject                # ...the creature game (entry `creatures`):
                                              # walk a route, get ambushed in long grass, fight or
                                              # run or throw a ball, level up and evolve, F5 saves.
                                              # A BALL IS AN ACTION (`Kind::Ball`, ch.138), not a
                                              # verb resolved beside `step` — a turn resolved
                                              # outside the resolver cannot appear in a recording,
                                              # and every fight writes one to
                                              # saves/creatures/last_battle.crep when it ends.
                                              # Whether a tile AMBUSHES you is `ground` id 3 in the
                                              # MAP and which table it rolls is a `far` MASK layer —
                                              # not arithmetic in world.cpp (the ch.134 rule, twice
                                              # more). Every verb has an on-screen control, laid out
                                              # by ONE creature::layout the renderer and the hit test
                                              # both read; the parts that are facts about a HAND
                                              # (44px, the proportion rule, the d-pad) are shared
                                              # with the farm in engine/ui/touch.hpp, the LAYOUTS are
                                              # not — one screen laid out for the other's neighbours
                                              # is exactly the bug sharing them would buy
./build/demo --project projects/farm.gameproject                     # ...the farm game (entry `farm`)
                                              # EVERY verb has an on-screen control (touch/mouse):
                                              # d-pad, Z/Q, the hotbar slots pick the tool, F5 saves,
                                              # and the dialogue is answered by tapping an option.
                                              # ALL of it — including the hotbar and the dialogue
                                              # panel — is laid out by ONE farm::layout/talk_layout
                                              # in src/games/farm/controls.hpp, which the renderer
                                              # and the hit test BOTH read: a control drawn in one
                                              # place and hit in another is invisible in a screenshot
                                              # resumes saves/farm/slot1.sav; signs in as a guest
                                              # (project pk_demo_farm), takes prices from remote
                                              # config + any live event, and reconciles the cloud
                                              # save (F5 save+push, F6/F7 resolve a conflict —
                                              # during a conflict those two REPLACE the save button,
                                              # because saving then silently means "mine wins")
./build/demo --project-inspect  <proj>        # validate/doctor + resource closure
./build/demo --project-package  <proj>        # deterministic package manifest (release-id seed)
./build/demo --project-publish  <proj> development "reason"   # atomic publish + audit
                                              # (the reason is REQUIRED: a blank audit line is not evidence)
./build/demo --release-promote  development preview "reason"  # dev -> preview -> production
./build/demo --release-rollback production <release-id> "reason"
./build/demo --release-status | --release-log [channel]
./build/demo --project-verify   <proj> development   # preview parity: exit 0 match / 2 drift / 1 err
./build/demo --hub <proj>                     # aggregate status + next recommended action
./build/demo --runner <baas_url> <api_key>    # headless BaaS test-run worker
./build/demo --pvp    <baas_url> <api_key>   # ONE rated creature match, headless (ch.139):
                                              # guest sign-in -> matchmaking -> a battle played
                                              # by exchanging ACTIONS (34 bytes a turn, 16 of
                                              # them the hash) -> the tape into the replay store
                                              # -> the outcome to the `creature_elo` ladder.
                                              # Two of these against one backend is a real match
                                              # between two processes; that is how the 500-turn
                                              # stalemate was found that every in-process test
                                              # passed through. Seeded project: pk_demo_creatures
./build/demo --bench-ui [frames] [proj]       # Studio frame cost, ss=1 vs ss=2 (no window)
./build/demo --cmd [id] [args...]              # run any registered command; no id lists them
```

Tests (dependency-free, no SDL/window needed):

```sh
ctest --test-dir build --output-on-failure     # all
ctest --test-dir build -R chess                # one suite by name (math, ecs, iso, fps, …)
./build/test_chess                             # or run the binary directly
```

**A battle that reaches `kMaxTurns` (200) is a DRAW** — a rule in `battle.hpp`, not a
valve on the outside, so the wild game, a stored replay and a rated match all get it.
It exists because a battle CAN stall: with no PP left nothing takes damage, and two
IDENTICAL teams then rotate their benches at each other forever. Every test built its
parties from random species, so no test had ever put two identical teams in a battle;
two real `--pvp` processes did it on the first try and ran 500 turns (chapter 139).
`choose` was fixed in the same commit to stop switching to a bench that also cannot
act — the two guards are tested apart, because with the AI fixed a mirror match now
*decides* and never reaches the cap.

**Every read-then-write in the backend is a TRANSACTION with a locking read**
(chapter 140). `db::lock_clause()` is `" FOR UPDATE"` on Postgres and `""` on SQLite,
and it is the ONLY syntactic difference between the two backends. The old comment —
*"atomic because the SQLite pool is size 1"* — was true of `purchase`, which opens a
transaction, and false of `grant` and `consume`, which did not: a pool of one hands
out the connection for a STATEMENT, not for a sequence. Two concurrent grants of a
player's first item did not lose an update, they **crashed the process** with an
uncaught `UniqueViolation`. `test_baas_concurrency` is eight threads on one purse and
**has no teeth on SQLite by construction** — it says so in its own header, and
`baas/ops/pg-test.sh` is the Docker harness that would give it teeth.
**⚠️ POSTGRES DOES NOT WORK** — not "untested": Drogon does not translate `?` to `$1`
and all 107 queries use `?`, `id INTEGER PRIMARY KEY` is not an auto-increment there,
and `insertId()` needs `RETURNING`. `pg-test.sh` reproduces it in one command.
**A transaction holds the only SQLite connection**, so anything reaching for
`db::client()` while one is alive self-deadlocks — the suite STOPPED for 25 minutes
before ctest's default timeout noticed, which is why every baas test now carries
`TIMEOUT 120`.

BaaS backend (separate process, **guarded on Drogon** — the engine build never
depends on it; when Drogon is absent its targets vanish from `ctest`, which is
**28 of the 83 tests**: `ctest` here reports 83, a build configured without Drogon
reports 55. Since chapter 129 CI has a `baas-test` job in the
`drogonframework/drogon` image that runs 27 of them — `sdk_realtime_live` needs
libcurl ≥ 7.86 and Ubuntu 22.04 ships 7.81, so it is skipped with a message rather
than silently. `cmake --build <dir> --target baas_tests` builds exactly that
directory's targets; `ctest --test-dir <dir>/baas` runs exactly its tests):

```sh
brew install drogon libsodium                  # enables the 'baas' target
cp baas/config.example.json baas/config.json   # gitignored local dev config
./build/baas/baas                              # or: docker compose -f baas/ops/docker-compose.yml up
```

CI (`.github/workflows/ci.yml`, ubuntu + macos) does a clean configure/build, runs
`ctest`, then smoke-tests the whole golden path (create → inspect → publish →
verify → promote → status → log → hub) and asserts no `.tmp` files leak from the
atomic publish. Breaking a headless verb breaks CI even if `ctest` is green.

Sanitizer build (ASan + UBSan) for memory/UB bugs during dev:

```sh
cmake -B build-asan -DENGINE_SANITIZE=ON && cmake --build build-asan && ./build-asan/demo
```

Web (Emscripten) build:

```sh
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-web && cmake --build build-web --target demo
cd build-web && python3 -m http.server 8765   # open http://localhost:8765/demo.html
```

Pick the scene with `?mode=` (`gui`, `farm`, `project`, `shell`, `hubui`, `colony`, …)
or point straight at a manifest with `?project=projects/farm.gameproject`. **`collection.html`
is the page you send someone**: it lists every `*.gameproject` as a card (cover, one line,
Play), decodes the `.hrt` cover in JavaScript, and renders the README beside the manifest.
The web build copies it, `assets/collection.json` and an ALLOWLIST of asset subdirectories
(`textures`, `projects`) next to `demo.html` — an allowlist, because `assets/` also holds
this machine's `saves/`. Serving it
from the BaaS instead (`./build/baas/baas --static build-web`) puts the page and the API
on one origin, which is what the SDK's relative base URL expects — the games that talk
to the backend only work that way.

The web build is **not** verified by linking. It had never been opened until chapter
118, and it did not run when it was. Since chapter 128 CI **runs the page**:
`node scripts/web_touch_check.mjs --dir build-web` drives Chrome over CDP with touch
emulation, dispatches a real `Input.dispatchTouchEvent` at the button the game printed,
and checks the player moved by reading `var px` out of the save the game wrote.

`web/shell.html` is a LINK-time input (`LINK_DEPENDS`),
`saves/`/`releases/`/`channels/` are `--exclude-file`d out of the preload (they are this
machine's state, and shipping `saves/device.id` gave every browser the same guest
account), and `saves/` is mounted on IDBFS by the page so the web build has any memory
at all.

## Layout & the library split

```
src/platform/   the platform seam (platform.hpp) + backend_sdl.cpp
src/engine/     hand-written core: math, rand (THE deterministic RNG — xorshift64*,
                because std::mt19937 is portable but its distributions are not),
                ui/touch.hpp (the parts of an on-screen control that are facts about
                a HAND — 44px, the proportion rule, the d-pad; each game keeps its own
                LAYOUT, because the rule is a discipline and not a shape),
                renderer2d, renderer3d, geometry, camera,
                assets, image, text, ui, ecs/, jobs/, memory/, physics/, anim/,
                fx/, audio/ + the platform spine: project/, resource/, release/, hub/
src/games/      one dir per scene/tool (chess, fps, iso, colony, creatures, studio,
                sandbox, hub, studio_shell, runner, …)
docs/book/      the guidebook (read the chapter for the subsystem you touch)
server/         hand-written HTTP server (POSIX sockets) — separate process, no engine code
baas/           Drogon Game-BaaS backend — separate process, links no engine code
sdk/cpp/        gbaas C++ SDK the game uses to talk to baas (native libcurl / web fetch)
```

`CMakeLists.txt` (root) lists sources **explicitly** — no globbing, on purpose.
Understand these deliberate patterns before editing the build:

- **Core logic is split into SDL-free static libs** (`chess_core`, `fps_core`,
  `render3d_core`, `iso_core`, `ecs_core`, `jobs_core`, `mem_core`, `physics_core`,
  `ui_core`, `text_core`, `viz3d_core`, `colony_core`, plus the platform-spine
  cores `project_core`, `inspect_core` (one read+validate+hash, shared by launch,
  package, publish and the Studio), `provenance_core` (where every `.hrt` came from,
  derived from the marks the three doors leave — the attribution rule, as a boolean), `resource_core`, `release_core`, `release_ops_core`,
  the game cores `creature_core` (a turn-based battle as INTEGER arithmetic —
  plus `netbattle` — ONE battle on TWO machines: each side sends the ACTION it chose
  (a kind and an index) and both compute the turn, then both send `hash(battle)` and
  compare. Which SIDE you are, the SEED and who you are matched with all arrive in the
  server's `matched` event — a seed mixed from two client halves lets whoever sends
  second grind theirs — and the wire carries `species:level` so a peer can lie about
  WHICH creatures it brings and not about what they are. A desync is caught by BOTH
  sides at the same turn and reports NOTHING to the ladder. Pure: frames in, frames
  out, no socket. `games/creatures/pvp.{hpp,cpp}` (a separate `creature_pvp` lib,
  because it links the SDK) is the glue `--pvp` and `test_creature_pvp_live` SHARE —
  types/moves/species as text, `step` returning string-free events, `hash` over the
  whole state, and `play` over a start state plus a list of actions; no float in the
  resolution path, the RNG is a hashed FIELD, and turn order is priority → speed →
  one draw from the battle's own stream, never "side 0 first"; plus `replay` —
  the battle AS A FILE (`crep1`): the start state (not a seed — parties arrive
  damaged), a hash after EVERY turn (so a divergence is reported where it happened,
  not where everything differs), and `rules_hash(dex)`, which is what lets `verify`
  say RULES MOVED instead of DESYNC when somebody re-tunes a move. The fingerprint
  covers what `step` reads and NOT the encounter tables or sprites — moving a
  creature to another patch of grass must not invalidate a recording of a fight
  against one; plus `world` — the
  loop AROUND the battle: walking a `tilemap::Map`, the encounter roll, experience,
  evolution that keeps the damage taken, the blackout, and a save that stores no
  stats because stats are a pure function of species and level; and `controls`),
  `farm_core` (day loop, crops, NPC schedules, dialogue, the pure
  cloud-save verdict `decide_sync`, the art `theme` — NAMED sheets, so imported
  and self-drawn art never share a file, plus `line_piece`, which picks one of a
  16-piece autotile LINE set from a cell's four neighbours, and `controls`, which
  lays out EVERY rectangle on that game's screen — d-pad, actions, save, the cloud
  conflict's two answers, the hotbar slots and the dialogue panel — so the renderer
  and the hit test cannot disagree; no renderer, no SDK),
  `inflate_core` (hand-written DEFLATE) and `png_core` (decode only, offline),
  `hub_core`/`hub_build_core`, and the content cores `studio_core`, `sandbox_core`
  (actors on a generic ECS; since chapter 133 an actor also carries an `Emitter`,
  a `Light`, a `Sound` and a flipbook clock — the model records what should be
  HEARD into `World::sounds` rather than opening a device, because a pure core
  must stay compilable into a headless test),
  `map_edit_core` (tile, entity AND autotile-rule edits as undoable `doc::Command`s),
  `particles_core`, `tween_core`, `light_core`, `audio_core` (all four now have a
  second consumer: an actor's `Emitter`/`Light`/`Sound`/flipbook, chapter 133 —
  the effect labs that used to be their only one are gone), `paint_core` (pixel
  edits as undoable commands — the third client of `doc::CommandStack` — plus
  `pixel_source`, the `.pix` -> `Image` bake, and `colour`, the HSV/hex arithmetic
  whose 8-bit round trip is exact),
  `runner_core`). Each has a matching `test_*` target so simulation/logic is
  unit-tested with no window.
- **`-DENGINE_BUILD_DESKTOP=OFF`** builds everything except the SDL2 `demo` target —
  that's how the backend container image builds without SDL2 present.
- **`renderer3d.cpp` / `ui.cpp` reference `Renderer2D` symbols but don't link it** —
  the final target that links them provides `renderer2d.cpp`. Several `test_*`
  targets therefore *compile* `renderer2d.cpp`/`assets.cpp` directly rather than
  linking a lib, to stay dependency-free. Tests needing asset files get
  `-DASSET_ROOT=...`.
- **`baas` is guarded on Drogon being installed** (`brew install drogon libsodium`).
  The ordinary engine build never hard-depends on it. The engine core gains **no**
  dependency from baas/sdk — only the SDK links libcurl.

## Runtime architecture

`App` (`src/engine/app.hpp`) owns the active `Scene` and a **fixed-timestep clock**
(`engine::FixedStep`, `src/engine/fixed_step.hpp` — header-only and pure, shared with
the Studio's Play viewport so there is exactly one spiral-of-death clamp):
`platform::run` feeds it a variable `dt`; `App::frame` accumulates it into fixed
`1/60 s` `update()` steps (deterministic logic) plus exactly one `render()` per frame.
A `Scene` (`src/engine/scene.hpp`) implements `update(dt, input)` and `render(ctx)`,
where `Context` carries the `Renderer2D`, input snapshot, timing, and shared UI font.
Each game is a `Scene`; `src/main.cpp` maps a CLI flag to a `platform::Config` + scene. The Studio's editors
implement `studioshell::Workspace` (`src/games/studio_shell/workspace.hpp`) — canvas +
inspector + status + save/undo/recovery. `WorkspaceHost` runs one full-screen (that is
what `--lab scene` is), and the Studio's Edit section runs them as tabs, so an editor
cannot exist in only one of the two frames. Manifest **entries**
live in one table there (`entries()`): `launch_entry`, `known_entries()` and the
Studio's Play viewport are all derived from it, so a game cannot be
launchable-but-unknown or known-but-unlaunchable.

## The platform spine (create → publish → promote → verify)

Beyond the games, the repo is growing a game-creation *platform*, and its data flow
is the thing most likely to be broken by a careless edit:

1. **`game.project` manifest** (`project_core`) — a versioned text file that declares
   identity, an `entry` (which game to launch), and its content as `asset <type> <path>`
   lines, plus the optional `summary` and `cover` a LIST of games needs and a launcher
   does not. A `cover` joins the resource closure (it ships, so it is hashed) unless the
   manifest already declared that same path — one file, one hash, or the release id moves
   without the content moving. `--project` launches from it; `src/main.cpp`'s `launch_entry` seam maps an
   entry name to a scene, so a new game needs no new CLI flag. **The farm game is the
   proof**: `projects/farm.gameproject` added a game with a manifest and a scene, and
   inspect/package/publish/hub all worked on it unchanged.
2. **Resource closure** (`inspect_core`) — `engine::inspect()` is the ONE
   read+validate+hash: launch, package, publish, the hub and the Studio's Project
   section all go through it, and it returns **data**, never printed lines. It reports
   **every** problem (validation errors before missing content), keeps a missing asset
   in place in the list, and computes a package hash **only** when the project is
   shippable — a release id must never be derivable from partial content. `--project`
   hard-refuses to launch with a missing dependency.
3. **Package** (`resource_core::build_package`) — resources sorted by path + a combined
   `packagehash`: order-independent, content-sensitive. This hash *is* the release id.
4. **Release store** (`release_core`, `release_ops_core`) — `releases/<hash>/` is
   immutable and content-addressed; the channels `development → preview → production`
   are pointers moved by promote/rollback. Publishes are **atomic** (stage `.tmp` →
   `assets::rename`) and **audited** (append-only `releases/audit.log` recording
   timestamp, predecessor, reason). Re-publishing identical bytes is a verified no-op;
   publishing *different* bytes under an existing id is refused.
5. **Hub** (`hub_core`) — one pure `hub_lines`/`recommend` shared by the headless
   `--hub` and the Studio's Hub section, so CLI and window can't drift.
6. **Command registry** (`commands_core`) — every operation registers once under a
   stable id; `--cmd <id>`, the Studio's `Cmd+K` palette and a button all go through
   `cmd::run`, so an operation cannot exist in only one of them. The old CLI flags are
   aliases onto it. Mutating commands **refuse blank arguments**: an audit line with
   no reason looks like evidence and answers nothing.

Two rules follow from that shape: **the operation lives in a pure `*_core` lib and the
trigger (CLI flag or keypress) only calls it** — never reimplement an op in a Scene; and
every write goes through `assets::` so the same code works on native and the web MEMFS.
The asset root (`ASSET_ROOT`/`assets/`) is the origin of every runtime path, which is
why manifests read as `projects/foo.gameproject`, not a filesystem path.

## Git workflow

`main` holds stable, reviewed checkpoints. Work happens on **one feature branch per
milestone**; each build step is its own commit; branches merge to `main` with
`--no-ff` so merge commits mark milestone boundaries. Never commit or push unless
asked.
