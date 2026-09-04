# Chapter 110 — One map format, and the migration that proves nothing changed

> Code: `src/engine/tilemap/map2.{hpp,cpp}` · `src/engine/tilemap/camera2d.{hpp,cpp}` ·
> `src/engine/tilemap/autotile.{hpp,cpp}` · `src/games/fps/map.{hpp,cpp}`
> (`from_shared_text`) · Tests: `tests/test_tilemap.cpp`, `tests/test_fps.cpp`

## Tóm tắt (VI)

Trước chương này repo có **hai lưới ô hoàn toàn độc lập**, và không cái nào dùng lại
được:

- `fps::Map` — mảng `uint8` dày đặc kèm định dạng text `fpsmap1`, **nằm bên trong**
  `games/fps`.
- `iso::TileMap` — mảng enum `Terrain`, **không có** định dạng text riêng.

Không cái nào có layer, mask va chạm, trigger, hay khái niệm entity. Hệ quả: không
thể tạo game 2D thứ hai mà không đẻ ra định dạng thứ ba, và Map Lab không thể thêm
bất kỳ tính năng nào trong số đó mà không làm hỏng raycaster.

`map2` là định dạng chung đó. Bốn quyết định đáng nhớ:

1. **`load()` tự nhận diện magic** — đọc được cả `map2` lẫn `fpsmap1`, tự migrate.
   Không caller nào phải biết file thuộc thời nào.
2. **Từ chối file version tương lai**, không đọc nửa vời. Parser cũ đọc schema mới
   là cách một công cụ âm thầm đánh mất các trường nó không hiểu rồi ghi mất mát đó
   ngược lại xuống đĩa.
3. **Ngoài bản đồ là tường.** Thế giới không có rào là thế giới nhân vật đi ra khỏi.
4. **"47" là *kết quả*, không phải hằng số chép tay** — bảng autotile được sinh bằng
   cách liệt kê đủ 256 mask, nên nếu quy tắc sai thì con số đổi và test kêu ngay.

Và điều quan trọng nhất: `--fps` **đã chuyển sang đường mới**, kèm một test chứng
minh nó **không đổi gì cả** — cùng một level đọc bằng hai đường phải giống nhau từng
ô và từng điểm spawn.

---

## 1. Two grids, neither reusable

The repository had been carrying two tile representations for a long time, and the
cost only becomes visible when you try to add the third game:

```cpp
// games/fps/map.hpp
struct Map { int w, h; std::vector<uint8_t> cells; int spawn_cx, spawn_cy; float spawn_dir; };

// games/iso/tilemap.hpp
enum class Terrain { Grass, Soil, Water, Path };
```

`fps::Map` has a text format; `iso::TileMap` does not. `fps::Map` lives inside a
game directory, so anything reusing it depends on `games/fps`. Neither has layers, a
collision mask, triggers or entities. Map Lab could not gain any of those without
changing the format the raycaster reads, and a farming game could not be authored at
all without inventing a third representation.

That is the actual shape of the problem: not "the map format is limited" but "there
is no map format, there are two private ones."

## 2. What the format has, and why

```
map2 1
name farm_home
size 64 48
tile 16
tileset ground tilesets/farm_ground.tsdef
layer ground  tiles ground
row …
layer collide mask
row …
entity npc_anna 12 7 sched=anna.sched dialog=anna.dlg
trigger door_house 10 3 2 1 target=house.map2
```

**Layers are one type, not two.** A mask is a layer whose ids happen to be 0 or 1.
Splitting them into separate concepts would double the grammar and the row parser to
express something the type system already handles.

**Properties are `key=value` words to end of line, and values may not contain
spaces.** That is a real limitation, taken deliberately: it keeps the whole format
parseable with a stream and greppable by eye. A value that needs spaces wants its
own file, which is what the `dialog=anna.dlg` reference already does.

**Tile ids are `int32`.** `fpsmap1` used `uint8`, and a 255-tile ceiling is
completely invisible until a tileset crosses it — at which point the fix is another
format version and another migration. Paying four bytes per cell now is cheaper than
paying a migration later.

**Out of bounds is solid.** `solid(x, y)` returns true outside the map. Every caller
would otherwise repeat the same bounds check, and the one that forgets produces an
actor that walks off the world.

**A version from the future is refused.** This is the one that looks like
pedantry and is not:

```cpp
if (version <= 0 || version > kFormatVersion) return std::nullopt;
```

An older parser reading a newer file will "work" — it will read the fields it
recognises and skip the rest. Then the editor saves, and everything it did not
understand is gone. Refusing to open is the only behaviour that cannot destroy data.

## 3. The migration, and proving it changed nothing

`fpsmap1` packs two facts into one number: the id picks the wall texture *and*
non-zero means solid. The migration separates them, because the rest of the engine
wants to ask "is this blocked?" without knowing about textures:

```cpp
wall.cells[i]    = v;              // appearance, verbatim
collide.cells[i] = (v != 0) ? 1 : 0;   // solidity, as its own layer
```

The optional `spawn` line becomes an entity, which is how every other authored point
in `map2` is expressed, with its facing carried across as a property.

`load()` sniffs the magic word and routes accordingly, so no caller learns that two
formats exist. `--fps` now goes through `fps::from_shared_text`, which narrows a
`tilemap::Map` back down to the dense `uint8` grid the DDA wants. The raycaster's own
data structure is untouched — it is a tight loop over an array, and there is no
reason for it to become something else.

That last change is the risky one, and it gets the test it deserves:

```cpp
auto legacy = from_text(text);          // the old path
auto shared = from_shared_text(text);   // through map2 and back
CHECK(shared->cells == legacy->cells);
CHECK(shared->spawn_cx == legacy->spawn_cx);
CHECK(shared->spawn_dir == legacy->spawn_dir);
```

read from the **real authored level** under `ASSET_ROOT` rather than from a fixture,
so it cannot pass against a copy that has drifted from what `--fps` actually loads.
A subtly wrong migration would otherwise ship as "the level looks slightly off",
which is the hardest kind of bug to attribute.

The connection matters as much as the code. A `tilemap_core` that nothing loaded
would be exactly the "motion without connection" the strategy document warns about —
a subsystem reachable only from its own unit test. Routing `--fps` through it means
the migration is exercised by the application and by CI's golden-path smoke on every
push, and the raycaster gained the ability to run a hand-authored `map2` level
without knowing that format exists.

## 4. A camera that does not make you seasick

`engine/camera.hpp` is 3D only — orbit and fly cameras producing view and projection
matrices. The only 2D camera in the tree was two pan floats inside `games/iso`, with
a comment noting that zoom was left as an exercise. So every 2D scene placed the
world on screen by hand, and none of them could scroll.

`Camera2D` has three behaviours, and each is there because leaving it out is
*visible*:

**Deadzone.** A box the target may move inside without the camera reacting.
Without it, every idle animation and every one-pixel wobble drags the whole screen.
It is applied to the target, not the camera: work out the nearest point that would
put the target back inside the box, and move toward that.

**Framerate-independent smoothing.** The naive version lerps by a constant fraction
*per frame*, which makes the camera literally faster on a faster machine — a
classic way a game feels different depending on hardware. The fix is one line:

```cpp
const float t = 1.0f - std::pow(1.0f - smooth_, dt);
```

and the test that pins it compares one 0.5 s step against fifty 0.01 s steps. The
mutation check confirms it: substituting the per-frame lerp fails that assertion.

**Whole-pixel snapping.** `origin()` rounds. A camera at a fractional offset makes
every sprite edge shimmer as it moves, because each edge lands on a different
sub-pixel each frame.

And one rule that is not about motion: a world **smaller** than the viewport is
centred, not clamped. Clamping is what falls out of the obvious `min`/`max`, and it
pins the small world into one corner with all the empty space on the other side.
Every inequality holds and it still reads as a bug.

## 5. Forty-seven is a result, not a constant

Eight neighbours give 256 combinations, but a blob tileset has 47 pieces. The
reduction has a reason: a **diagonal neighbour only matters when both of the
cardinals beside it are also filled.** A corner that meets nothing along its edges
has nothing to blend into, so the diagonal cannot change what the piece looks like.

```cpp
if (!((mask & kN) && (mask & kE))) out &= ~kNE;
```

Four lines like that, and 256 masks collapse into 47 equivalence classes.

The table is then built by **enumerating all 256 masks** and assigning an index to
each new canonical form:

```cpp
for (int m = 0; m < 256; ++m) {
    const uint8_t c = autotile_canonical(uint8_t(m));
    if (seen[c] < 0) seen[c] = count++;
    index[m] = seen[c];
}
```

so `autotile_count()` returns 47 as a *consequence*. If the canonical rule were
wrong the number would change, and `CHECK(autotile_count() == 47)` says so
immediately — which the mutation check demonstrates by deleting one diagonal rule.
Typing out 47 constants would have produced a table that is right only as long as
nobody looks at it.

There is deliberately **no tileset file format** here. The rule is the part with the
algorithm in it; mapping those 47 indices onto actual artwork needs an editor to
author it, and building the file format now — with no map using it and no tool
writing it — is exactly the pattern `docs/strategy/02` §10b names as the project's
own recurring mistake.

## What is verified, and what is not

Verified, by running it:

- 63 tests green. The tilemap suite covers exact round-trip, every query including
  out-of-bounds and unknown layers, trigger rect overlap, eight malformed-input
  rejections, the full migration, camera deadzone/bounds/centring/smoothing/culling,
  and all 256 autotile masks.
- The **real authored level** migrates identically: `test_fps` reads
  `assets/maps/level_00.map` through both the legacy parser and the new shared one
  and compares grid, spawn position and spawn facing.
- Two **mutation checks**: a per-frame lerp fails the camera's framerate test;
  dropping one diagonal rule moves the autotile count off 47 and fails 40+
  assertions.
- The Emscripten build still links.

Not verified:

- **`--fps` was never watched running through the new path.** The conversion is
  proven equal to the old one by test, and the scene is a two-line call into it, but
  no window was opened (screen capture is unavailable here).
- **No map2 file has been authored by a human.** Map Lab still writes `fpsmap1`; the
  only `map2` maps that exist are the ones the tests build in memory. Layers,
  triggers and entities are therefore proven to parse and query, and unproven as
  something an editor produces.
- **`iso::TileMap` was not migrated.** It has no text format to migrate and its
  consumers (the iso farm sim, colony's pathfinding) work; converting them now would
  be churn with no consumer asking for it.
- **Nothing renders a map2 map yet.** Culling and y-sorted entity drawing are
  described in the plan and are not in this chapter; they arrive with the first game
  that needs them.
