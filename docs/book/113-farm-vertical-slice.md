# Chapter 113 — A second game, reached through a manifest

> Code: `src/games/farm/{defs,world,dialogue,farm_scene}.{hpp,cpp}` ·
> `src/engine/document/save.{hpp,cpp}` · `src/engine/tilemap/camera2d.hpp` (viewport re-clamp) ·
> `src/main.cpp` (`launch_entry`) · Data: `assets/farm/`, `assets/maps/farm_home.map2`,
> `assets/projects/farm.gameproject` ·
> Tests: `tests/test_farm.cpp`, `tests/test_farm_scene.cpp`, `tests/test_tilemap.cpp`

## Tóm tắt (VI)

Game thứ hai — và là game đầu tiên đi vào bằng **manifest**, không phải bằng một flag
CLI mới. `--project projects/farm.gameproject` đọc `entry farm` rồi `launch_entry` ánh
xạ sang scene. Đó chính là điều mà cái seam đó tồn tại để làm: **thêm một game = một
manifest + một scene**, không phải thêm một nhánh `if` trong `main.cpp`.

Bốn điều đáng nhớ:

1. **Mô phỏng tách hẳn khỏi cách vẽ.** `farm_core` không có renderer: đồng hồ, năng
   lượng, cuốc/tưới/trồng/thu, lịch NPC, dialogue, save. Nhờ vậy `test_farm` **chơi
   ba ngày** và kiểm parsnip lớn đúng lịch — không cần cửa sổ.
2. **Crop và item là DATA** (`assets/farm/*.def`), không phải literal C++. Cân bằng số
   liệu mới là công việc, và nó **không nên cần build lại**. Số sai (`days=four`) là
   **lỗi**, không phải 0 âm thầm — nếu không thì cây không bao giờ lớn mà chẳng có
   dòng nào nói vì sao.
3. **Mutation test tìm ra bug thật, không phải xác nhận tick xanh.** "Chín" từng được
   suy ra từ chỉ số stage đã làm tròn, nên cây mà số stage không chia hết số ngày sẽ
   **thu hoạch được sớm một ngày**. Parsnip (4 ngày / 5 stage, chia hết) không bao giờ
   lộ ra. Nay **chín** và **hình dạng** là hai quyết định tách biệt.
4. **`Camera2D` có consumer đầu tiên — và lộ ra một bug thật.** `set_viewport` không
   clamp lại, nên một thế giới nhỏ hơn cửa sổ không được căn giữa ở frame đầu (và một
   lần resize cửa sổ sẽ đẩy view ra ngoài thế giới). Cùng một khuôn mẫu như chương 112:
   **cái core nào chưa có người dùng thì cái interface của nó chưa từng bị cãi lại.**

Save có **version + chuỗi migration**. Save của bản **mới hơn bị từ chối** — đọc rồi
ghi lại là mất dữ liệu đội lốt tương thích; **thiếu một bước** trong chuỗi cũng bị từ
chối chứ không nhảy qua.

---

## Why this game, and why now

Five slices in a row were platform work. The strategy's rule is that the golden path
has to *consume* what the platform grows, and there was exactly one game behind the
manifest — the raycaster, which predates the manifest and was retrofitted onto it. One
consumer cannot tell you whether a seam is a seam or a coincidence.

So this chapter's real question is not "is the farm fun". It is: **can a second game
reach the platform without the platform changing shape?** The answer turned out to be
almost — one bug in the camera, and nothing else.

The things it consumes, none of which existed for it:

| Piece | Chapter | What the farm needed from it |
|---|---|---|
| `game.project` manifest + `launch_entry` | 90 | a game selected by a file, not a flag |
| `map2` layers, entities, collision | 110 | a world with a house, a pond, and named places |
| `Camera2D` deadzone + smoothing + bounds | 110 | a farm larger than the window |
| Map workspace | 112 | somewhere to edit the map that is not a text editor |
| `assets::` seam | 40s | data files and a save that work on native and web |

`map2` entities earn their keep here in a way the format alone could not show: an NPC's
schedule names **places**, and a place is a map entity. Moving Anna's shop is a map
edit, not a code edit, and the schedule file never mentions a coordinate.

## The shape

```
farm_core  (no renderer, no I/O)
  defs      crops.def / items.def  ->  CropDef, ItemDef
  world     clock, energy, soil, tools, end_day, schedules, save/load, hash
  dialogue  .dlg -> nodes; a runner with say / choose / goto / end
FarmScene  (no SDL)
  reads the map + defs through assets::, drives World, draws it, owns the camera
```

The split is the same one the whole project keeps making, and it keeps paying: the
day loop is a unit test, and the scene is a thin thing that turns key presses into
`use_tool` calls and a `World` into pixels.

## Time is the resource

A day runs 06:00 to 02:00, and 12 real minutes covers it. Two details matter more than
they look:

**The clock is an integer; only the remainder is floating point.** A frame at 60 fps is
1/60 s, and a game minute is 0.6 s — so truncating `dt / 0.6` per call yields zero, and
time never moves at all. The accumulator drains whole minutes, which means every
simulation decision lands on a whole minute while the day still takes the same
wall-clock length at 30 fps and at 144. There is a test that runs ten seconds at both
rates and asserts they reach the same minute.

**02:00 reports once.** `advance()` returns true on the transition and false forever
after, so a caller cannot be sent to bed twice by two calls in the same frame.

## Growing things, and the bug that was hiding

The first version derived the growth stage from a rounded fraction of the crop's days,
and treated "the last stage" as "ripe":

```cpp
const int stage = (days_grown * (stages - 1) + days - 1) / days;   // round up
s.stage = std::min(stage, stages - 1);
if (s.stage == stages - 1) ++ripe;
```

Every test passed. The parsnip — 4 days, 5 stages — grew one stage per watered day and
ripened on day four, exactly as designed.

It was wrong. For a crop where the stage count does not divide the day count, rounding
up reaches the last stage early: a 5-day, 3-stage pumpkin was harvestable after **four**
waterings. The parsnip's numbers divide evenly, so the only crop in the test could
never have shown it.

The fix is not better rounding. It is to stop deriving one thing from the other:

```cpp
if (s.days_grown >= c.days) { s.stage = c.stages - 1; ++ripe; }   // ripeness: the calendar
else s.stage = std::min(c.stages - 2, s.days_grown * (c.stages - 1) / c.days);  // looks
```

Ripeness is the calendar. The stage is how it looks. They were never the same fact, and
tying them together made a rounding choice into a game-balance bug.

There is now a 5-day/3-stage crop in the test that fails if they are conflated again,
and a second check that the intermediate stages actually move — because the obvious
"fix" (never advance the stage until ripe) would pass the first check and make the crop
invisible for four days.

> The general lesson is about fixtures, not arithmetic. **A test whose only fixture has
> a convenient property tests the property, not the code.** 4 and 5-1 happen to be
> equal, and that coincidence was doing the work.

## The camera's first consumer

`Camera2D` was written in chapter 110 with deadzone, framerate-independent smoothing,
pixel snapping and bounds that CENTRE a world smaller than the viewport. It had tests.
It had no consumer.

The farm is the consumer, and the first frame came out with the map shoved into the
right half of the screen. The cause: a scene learns how big its framebuffer is when it
first *draws*, which is after it has already positioned the camera. `set_viewport` set
two ints and did not re-clamp, so the camera kept a position that was legal for a
zero-sized viewport and illegal for the real one.

`set_viewport` and `set_bounds` both re-clamp now. Both are relationships between the
world and the view: changing either can make the current position illegal, and a window
resize is the same event in a different costume. The test states it as the scene hits
it — bounds, then `snap_to`, then a viewport arriving late — plus the resize direction.

## Saves that can survive their own format

`doc::SaveState` is generic (it lives in `document_core`, not in the farm): a game id, a
version, flat `vars`, and opaque `sections` a subsystem serializes itself. The save
layer does not need to understand a crop grid to store one.

Three rules, each one a way that saves normally get destroyed:

- **A save from a newer build is refused.** Reading it with an older parser drops the
  fields that parser does not know, and the next save writes that loss to disk. Refusing
  is the only behaviour that does not lose data.
- **A gap in the migration chain is refused**, not skipped. Jumping a version leaves the
  data in a shape nothing has ever written.
- **The runner owns the version bump**, so a migration only has to describe the data
  change. A migration author cannot cause an infinite loop by forgetting.

The chain is empty at version 1. It exists, and is tested with a synthetic two-step
chain, so that the first real migration is an *addition* rather than a design.

Writing the same state twice produces the same bytes (`std::map` iterates in key order),
so a save can be diffed and content-hashed like everything else here.

## Dialogue as data

`.dlg` is four verbs: `say`, `choice`, `goto`, `end`. Two rules are in the parser rather
than the runtime, because that is where a content bug should surface:

- **A node with no exit is a parse error.** It would strand the player in a text box.
- **Every jump is resolved at parse time.** A broken link is found when the file loads,
  not when a player happens to pick that option.

And one rule is in the runtime: **choices stay hidden until the last line of their node
is on screen.** Offering options while there is still text to read asks the player to
answer a question they have not finished hearing.

The typewriter is the scene's, not the runner's — the runner has no clock. The first
press of the action key finishes the current line rather than skipping it, so a fast
reader and a fast presser do not lose text they never saw.

## What is verified, and what is not

**Ran, and the result was checked:**

- `ctest` — **69/69 green**, including `farm` (the simulation) and `farm_scene` (the
  game driven headless through synthesized key presses).
- **ASan + UBSan clean.**
- **Mutation-tested**: the growth rounding (which found a real bug — see above), the
  schedule's overnight fallback, the clock accumulator, the dialogue choice-timing rule,
  and the migration version bump each turned assertions red when broken.
- `test_farm_scene` renders the game offscreen at 640×360×ss2 and asserts the world is
  on screen, that **working the field changes the picture** (a simulation the render
  ignores looks exactly like a frozen game), and that the night tint darkens it. The
  three frames were dumped and **looked at**.
- `--project-inspect`, `--project-package` and `--hub` all work on
  `projects/farm.gameproject` with no change to any of them — which is the claim this
  chapter is really making.
- Emscripten build green with the new game in it.

**Not verified — stated plainly:**

- **The game has never been played in a window**, only driven by synthesized input.
  Nothing here has felt a keyboard: step timing, how the camera reads while walking, and
  whether 12 real minutes per day is right are all unmeasured. That last one especially
  is a *feel* question, and feel questions cannot be answered by a test.
- **No art.** Tiles are flat colours, the player and the NPC are circles. It is honest
  about what exists, and it is not what a player would be shown. The external CC0
  tileset the plan calls for is still an open decision, along with the game's real name
  — *Farm* is a working title.
- **The map was generated, not drawn.** `farm_home.map2` was produced by a script and is
  editable in the Studio's Map workspace, but nobody has sat down and laid out a farm.
- **One map, no transitions.** `map2` triggers exist and are parsed; nothing uses one
  yet, so there is no house interior and no town.
- **No shop, no seasons, no weather, no friendship, no fishing.** MVP is the loop —
  till, plant, water, sleep, ship — and everything else is v1 (chapter 117's slice).
- **Cloud save is not wired.** The save is local text through `assets::`; the BaaS
  round trip is a later slice, and the format was designed with it in mind (whole-text
  push with a version) rather than built for it now.
