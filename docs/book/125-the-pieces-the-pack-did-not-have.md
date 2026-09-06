# Chapter 125 — The pieces the pack did not have

> Code: `src/engine/paint/pixel_source.{hpp,cpp}` (new) · `src/engine/commands/asset_commands.cpp` ·
> `src/engine/tilemap/autotile.{hpp,cpp}` · `src/games/farm/theme.{hpp,cpp}` ·
> `src/games/farm/farm_scene.{hpp,cpp}` ·
> Art: `assets/textures/farm_path.pix` (new) → `assets/textures/farm_path.hrt` (new) ·
> `assets/farm/theme.def` · `assets/ATTRIBUTION.md` ·
> Tests: `tests/test_paint.cpp`, `tests/test_commands.cpp`, `tests/test_tilemap.cpp`,
> `tests/test_farm.cpp`, `tests/test_farm_scene.cpp`

## Tóm tắt (VI)

Chương 123 tìm ra vật cản **đúng thứ tự**: autotile con đường của farm **không** bị
chặn bởi luật autotile — `autotile_index` có từ chương 110 — mà bị chặn bởi **ART**.
Đường rộng **một ô**, pack Kenney chỉ có **mảng đất 9 mảnh** (loại để lấp một *vùng*),
và không có nhiễu nào sinh ra được một đầu mút hay một khúc cua. Phải có người **đặt
pixel**.

**Nhưng Pixel workspace không phải chỗ để LÀM RA một BỘ.** Lý do rất cụ thể, không
phải sở thích: một bộ autotile 16 mảnh **không phải mười sáu bức vẽ**. Nó là **một
profile hành lang và một khối trung tâm, cắt mười sáu kiểu**, và mọi mảnh phải **khớp
với nhau ở đường ghép**. Thứ quan trọng là **QUAN HỆ GIỮA các mảnh** — mà quan hệ thì
người ta **đọc, diff, review**, chứ không xác minh bằng cách bấm qua mười sáu canvas.

Nên: **nguồn là text, artefact là `.hrt`** — đúng quan hệ mà `.recipe` có với texture
sinh ra (ch. 122). Và cùng một luật: `.hrt` trong repo **phải là** cái mà `.pix` trong
repo bake ra, và **test nói thế**, không phải commit message.

**`autotile_index` (47 mảnh) vẫn chưa có người dùng — và chương này tìm ra TẠI SAO thay
vì ép nó.** Luật 47 dành cho một **VÙNG**. Đường là một **ĐƯỜNG THẲNG**: rộng một ô thì
**không đường chéo nào** có đủ hai ô cạnh, nên `autotile_canonical` xoá sạch đường chéo
và chỉ **16 trong 47** mảnh là với tới được — rải rác trong một sheet 47 ô mà **31 ô
không bao giờ vẽ được**. Nên đường thẳng có cách đánh số riêng, và **điểm của nó là
ART**: chỉ số **CHÍNH LÀ** mặt nạ 4 bit, nên vị trí trong lưới 4×4 **là nghĩa** của
mảnh. Ô 5 là bắc|nam, ô 15 là ngã tư.

**Hai mutation sống sót, và cả hai chỉ ra cùng một lỗ.** Test scene so **cả khung
hình** (toạ độ sẽ là đang test camera) nên nó chứng minh được "mảnh **thay đổi** theo
ô" mà **không** chứng minh "mảnh **đúng**". Sửa: đưa bộ chọn mảnh ra thành **hàm thuần
`farm::line_piece`** trên một `Map` — đúng luật D của spine: *phép toán sống trong core,
trigger chỉ gọi*. Rồi kiểm **cả mười sáu** vùng lân cận, và năm ô mốc trên chính bản đồ
của farm.

## The blocker was art, and it stayed art

Six chapters of the farm have had a dirt path drawn as one tile repeated: `tile ground
2 town 40`. It reads as a chain of squares, because that is what it is. Chapter 122
noticed why fixing it was not a code job and chapter 123 confirmed it: Tiny Town ships
a nine-piece dirt **patch** — the corner/edge/centre set you fill an *area* with — and
the farm's path is one tile wide.

A one-wide path is a **line**, and a line needs a different set: two end caps per axis,
four elbows, four T-junctions, a crossroads, and the isolated stub. Sixteen pieces.
Nobody had them.

## Why the pixel editor was the wrong tool for this

Chapter 123 built the Pixel workspace with this exact job in mind, and then this
chapter did not use it. That is worth being precise about, because it is not a
retraction.

The workspace is where a **pixel** gets tweaked. This is a **set**, and a set has a
property a single image does not:

> A sixteen-piece autotile set is not sixteen drawings. It is one corridor profile and
> one centre block, cut sixteen ways, and every piece has to agree with every other one
> along its seams.

The load-bearing content is the **relationship between** the pieces. Get one arm three
pixels off and the straight run grows a rung every sixteen pixels — and **no screenshot
of any single tile shows it**. A relationship is something you read, diff and review.
Sixteen canvases is the wrong shape of tool for it, the same way a spreadsheet is the
wrong tool for a paragraph.

So the source is text:

```
size 16
grid 4 4
palette . 00000000
palette d eaa56c
tile 5                 # north|south — the vertical run
...DddddddddD...
...
```

and `asset.pixels` bakes it to `.hrt`. That is exactly the relationship `.recipe` has
to a generated texture, which means the same rule follows and the same test shape
applies: **the committed `.hrt` must be what the committed source bakes to.**

The three doors into `.hrt` are now complete, and they are complete in a way that
matters: *imported*, *generated*, *drawn*. One format downstream, three origins, each
with a line in `ATTRIBUTION.md`.

### The refusal that is actually the point

```cpp
for (int i = 0; i < cols * rows; ++i)
    if (!tiles.count(i)) return fail("tile " + std::to_string(i) + " was never drawn");
```

Every other refusal in this parser is ordinary strictness. This one is the format's
reason to exist. **A gap in an autotile set does not appear where it was made** — it
appears the first time somebody redraws the map into a shape that needs the missing
piece, weeks later, as a hole in the ground. Refusing an undrawn slot moves that
failure back to the file that caused it.

The same instinct runs through the rest: a character no `palette` line declared is
refused rather than treated as transparent, and every message names the **line**. A
pixel format that eats a typo produces a hole nobody looks at twice.

One small thing the format cannot have: a colour cannot be written `#rrggbb`, because
`#` opens a comment and one character cannot mean two things. The test pins the
refusal rather than the omission, since writing `#` is a universal habit.

## `autotile_index` still has no consumer, and now we know why

Chapter 110 built the 47-piece blob rule and nothing has used it since. The obvious
move here was to finally give it one. It is the wrong move, and finding out why is the
useful part of this chapter.

The blob rule is for an **area**: a grass patch, a lake, a plateau — a material that
spreads in two dimensions, where a diagonal decides whether a corner is inside or
outside. Down a **one-wide path** no diagonal ever has both of its cardinals filled, so
`autotile_canonical` erases all four of them and only **16 of the 47** indices are
reachable — scattered across a 47-slot sheet of which 31 slots could never be drawn.

```cpp
int autotile_line_index(std::uint8_t mask) {
    return ((mask & kN) ? 1 : 0) | ((mask & kE) ? 2 : 0) |
           ((mask & kS) ? 4 : 0) | ((mask & kW) ? 8 : 0);
}
```

Deliberately a bit shuffle and not a lookup table, because **the numbering is the sheet
layout**. Tile 5 is north|south; tile 15 is the crossroads. Anything that hides the
mapping — a table, an enum of sixteen names — puts a step between "tile 5" in the art
file and "north|south" in the code, and that step is where a piece ends up in the wrong
slot. `test_tilemap` spells the eight landmark values out for the same reason: a
reordering would silently swap a corner for a T-junction, and every "the pieces are
distinct" style of assertion would still pass.

Both rules now sit side by side, because both are true. Choosing between them is the
authoring decision: **is this material a region or a road?**

## One theme record, and one refusal it forced

```
autotile ground 2 path 0
```

`index` becomes the **base** of sixteen consecutive tiles and the cell's own neighbours
pick which. The map still stores one id — `2` — everywhere along the path. Which corner
piece a cell wears is a **consequence of the map**, never something an author renumbers
by hand; that is the whole point of the semantic id, and it is why redrawing the path
in the Map workspace makes the pieces follow with no change to the `.map2` file.

It also forced a refusal the format did not have: **two lines for one id**. That copies
the duplicate-sheet-name rule, but it matters more here, because `tile` and `autotile`
disagree about what `index` *means*. Whichever line lost would change the picture, not
just the file it came from.

## Two mutations survived, and they were the same hole

| mutation | first verdict |
|---|---|
| `line_piece` never checks the north neighbour | **SURVIVED** |
| any non-empty neighbour connects, not just the same id | **SURVIVED** |

The scene test compares whole **frames** — it renders the shipped theme, then
`tile ... 0` (the picture you get if the `autotiled` flag is ignored) and `tile ... 5`
(the picture if the mask were constant) and requires all three to differ. Frames rather
than pixel probes on purpose: where any tile lands depends on the camera, so a
coordinate in that test would be testing the camera.

That catches every way of making the path **uniform**, and no way of making it
**wrong**. Both survivors still vary the piece cell by cell; they just choose a
different one, and all three frames in the comparison were rendered by the same
mutated code.

The fix was not another rendering assertion. It was to notice that the chooser had been
written as a private method on the scene, which is the one shape in which it cannot be
checked against a real map without a renderer — and the spine's own rule already says
otherwise:

> the operation lives in a pure `*_core` lib and the trigger only calls it

So `farm::line_piece(map, layer, id, x, y)` moved into `farm_core`, and the test asks
the real question: build a 3×3 map for **each of the sixteen** neighbourhoods (with all
four diagonals filled every time, so a rule that consulted one would answer sixteen
different questions) and check the answer is the mask. Then five landmark cells on the
farm's own map — the north end cap, a straight run, the corner, a horizontal run, the
east end cap — and one cell at the map's edge, because out of bounds must **not**
connect or a path touching the border grows an arm running off the world.

All four re-runs died, along with two more aimed at the same code.

### And one redundant guard, again

`line_piece` was written with `map_.in_bounds(nx, ny) && map_.at(...) == id`. `Map::at`
already answers `0` outside the map and `parse_theme` already refuses to map id 0, so
the guard could never change an answer. Deleted — it is the fourth chapter in a row to
find one (121, 122, 123, and here), and it is still the shape a mutation hides in.

## The seams are the test

```cpp
CHECK(n ? row(m, 0) == north : blank(row(m, 0)));
```

For every piece: if it connects north, its top row is the shared profile; if it does
not, its top row is empty. Same for the other three sides, plus the shared profile of
the north edge equalling the one of the south edge — which is what makes a straight run
continuous rather than a ladder — plus the four outer 3×3 corners staying grass in
every piece, because an elbow that filled its outside corner is a square, and sixteen
squares are what this set replaced.

That is what makes the `.pix` safe to edit by hand. Break a seam and the test says so,
at the piece.

## What was checked

| gate | result |
|---|---|
| `ctest` | 76/76 |
| ASan + UBSan | 76/76 clean |
| mutations | 12 run, 12 killed (2 only after moving the chooser out of the scene; 1 redundant guard deleted) |
| art | the sheet rendered and looked at; the farm rendered and looked at — the path is one continuous road with an end cap at each end and a proper corner |
| golden path | inspect → publish → verify (exit 0) → hub, green |
| web build | green |

## Ceilings

- **`autotile_index` (47) still has no consumer.** It is now *explained* rather than
  unused, but the blob set has no art either: Tiny Town's nine-piece patch is not a
  47-blob, so the first material that wants it will need its own drawing job.
- **The `.pix` and the `.hrt` can drift.** Same as `.recipe`: the source is not in the
  manifest, and editing the `.hrt` in the Pixel workspace does not update the text.
  `test_commands` catches the drift, which is not the same as preventing it.
- **One layer of colour, no transparency blending in the source.** A character is one
  RGBA value; there is no way to say "the pack's dirt at 50%".
- **The path is the only autotiled material.** Grass, water and every decor id are still
  one tile each, and a second line material would need a second sixteen-piece sheet —
  nothing is shared between them yet.
- **The corridor width is a constant in the art, not a parameter.** Ten pixels of
  sixteen, chosen by eye against Kenney's fence. Making it narrower is sixteen edits.
- **The Pixel workspace still cannot create a file.** It edits the sheets a project
  declares; there is no New. `asset.pixels` writes one, which is a door, not a fix.
- **The 7 KB `.pix` source ships in the web preload**, like the 5 KB import PNG before
  it (chapter 121). Deliberate, for the same reason: the provenance travels with the
  artefact so the bake can be re-run rather than taken on trust.
- **Nothing measured.** The draw path gained four `Map::at` calls per autotiled cell and
  no before/after frame cost was taken.
