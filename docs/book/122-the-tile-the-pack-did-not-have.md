# Chapter 122 — The tile the pack did not have

> Code: `src/engine/commands/asset_commands.{hpp,cpp}` ·
> `src/games/studio/recipe.{hpp,cpp}` ·
> `src/games/farm/theme.{hpp,cpp}` · `src/games/farm/farm_scene.{hpp,cpp}` ·
> Data: `assets/textures/farm_water.recipe` (new), `assets/textures/farm_water.hrt` (new),
> `assets/farm/theme.def`, `assets/projects/farm.gameproject`, `assets/ATTRIBUTION.md` ·
> Tests: `tests/test_commands.cpp`, `tests/test_farm.cpp`, `tests/test_farm_scene.cpp`,
> `tests/test_shell_golden.cpp`

## Tóm tắt (VI)

Chương trước kết thúc bằng một câu thừa nhận: **cái ao vẫn là hình chữ nhật xanh phẳng**,
vì Tiny Town không có tile nước. Chương này vẽ nó — và chỗ khó **không phải** là vẽ.

Quyết định 2 của anh có hai nửa. Nửa đầu (pack CC0) xong ở chương 121. Nửa sau là
"**clone vẽ lại trong Studio**", và nó đặt ra một câu hỏi mà chương trước không phải trả
lời: **art của mình sống ở đâu?**

Câu trả lời sai nhưng hấp dẫn: vẽ thêm một ô vào sheet của Kenney. Làm thế thì file dẫn
xuất **không còn ghi công được** (một file, hai chủ), và **lần re-import sau xoá sạch**
công sức đó. Nên: **sheet có TÊN, và có nhiều sheet.**

```
sheet town  textures/town.hrt       16      # pack CC0, import từ PNG
sheet water textures/farm_water.hrt 16      # một tile project tự làm

tile ground 1 town  0
tile ground 3 water 0                        # cái ao
```

Hai id **cùng một layer** giờ trỏ vào **hai file khác nhau**, và map không biết cũng
không cần biết. Đó mới là "hỗ trợ cả hai" thật: **không phải hai đường code, mà một bảng
nối, mỗi dòng tự khai nguồn của nó.**

**Cánh cửa thứ hai vào `.hrt`.** Texture Lab đã ghi `.recipe` bên cạnh mỗi `.hrt` từ
chương 88, và **chưa từng có gì đọc lại** ngoài nút Load của chính nó. Nghĩa là pixel
trong repo chỉ là **lời khai**. Giờ có `asset.texture <src.recipe> <dst.hrt>`, và
`test_farm` **sinh lại tile từ recipe rồi so từng byte** — "vẽ trong Studio" thành **sự
thật kiểm được**.

**Một lỗi im lặng phải chặn:** `from_recipe` **không thể fail** — key thiếu thì lấy mặc
định, đó là cái làm format tương thích tiến. Với một lệnh **GHI file**, sự khoan dung đó
là cái bẫy: trỏ nhầm file thì nó **bake texture mặc định đè lên đích** rồi báo thành công.
Giờ nó đếm số key nhận ra, và lệnh từ chối khi số đó bằng 0.

**Một mutation sống sót, và nó chỉ ra một lỗ hổng test thật**: guard "index vượt quá số
tile của sheet" **chưa bao giờ được chạm tới**, vì không file nào trong repo có index sai.
Sửa bằng **test**, không phải bằng xoá guard: dựng scene thứ hai trên **bản sao** cây
asset, với một dòng theme trỏ ra ngoài sheet.

---

## The half of the decision that was still a sentence

Chapter 121 shipped an open-licence pack and left the pond flat, on purpose, as the proof
that the theme is per-tile. The remaining half of the decision — *clone and redraw in the
Studio* — sounds like a task about pixels. It is not. It is a question about **where art
this project makes is allowed to live**, and the tempting answer is wrong.

The tempting answer: open Kenney's sheet, paint a water tile into an unused cell, done.
One file, one import path, nothing new to build.

It breaks two things that are load-bearing:

- **Attribution.** `assets/ATTRIBUTION.md` exists so every pixel can be traced to a
  licence. A sheet that is *mostly* CC0 with our pixels mixed in is a file with two owners
  and no honest line in that table.
- **Reproducibility.** `town.hrt` is derived: `asset.import` regenerates it from the
  committed PNG in one command, and chapter 121 made a point of that. Painting into the
  derived file makes the import destructive — re-run it and the work is gone.

So the theme grew named sheets. Not for generality; to keep those two properties.

## Sheets have names

```
sheet town  textures/town.hrt       16
sheet water textures/farm_water.hrt 16

tile ground 1 town  0      # plain grass, from the pack
tile ground 3 water 0      # the pond, from the Texture Lab
```

`ground 1` and `ground 3` are the same layer, ten characters apart, resolving to two
different files with two different origins. Neither file knows the other exists; the map
stores neither path.

That is what "support both sources" means once it stops being a slogan. Not two code
paths — **one join table, where each line names its own source.** Adding a third source
later (a second pack, a commissioned tileset, a friend's sprites) is a `sheet` line, not a
design change.

### The refusal the format made necessary

Every format change adds a way to be wrong, and the job is to find the one that would be
**silent**. Here it is exact:

```
tile ground 1 twon 0
```

`twon` is a typo. Without a check, the line points at a sheet that does not exist, the
lookup yields nothing, and the tile falls back to flat colour — which is
*indistinguishable* from "this id has no art yet". The file looks correct, the game looks
slightly wrong, and nothing says why. So a `tile` line naming an undeclared sheet is a
parse **error**.

Two `sheet` lines claiming one name is refused for the same class of reason: one of them
loses, and which one depends on line order. Nothing about that is a decision anybody made.

## The other door into `.hrt`

```sh
./build/demo --cmd asset.texture textures/farm_water.recipe textures/farm_water.hrt
# baked textures/farm_water.recipe -> textures/farm_water.hrt  (16x16, 12 keys)
```

Chapter 121 drew the diagram with one arrow already dashed:

```
PNG (any pack)   ──asset.import ──▶  .hrt  ──▶ engine
Texture Lab      ──asset.texture──▶  .hrt  ──▶ engine
```

The second arrow existed as a window and a Save button. That is enough to *make* a texture
and not enough to **prove** one: the Lab writes a `.recipe` sidecar beside every `.hrt` so
a texture can be re-edited, and until this chapter nothing but the Lab's own Load button
ever read one back. The pixels in the repo were a claim.

Now they are checked. `test_farm` reads the committed recipe, regenerates the image, and
compares bytes:

```cpp
CHECK(applied == 12);                                    // nothing defaulted silently
CHECK(gfx::encode_hrt(studio::generate(p)) == *baked);   // byte for byte
```

Which also pins something the generator's header has always claimed and nothing enforced:
that it is deterministic and pure. A texture that cannot be regenerated is a texture that
cannot be edited — only re-admired.

### A tolerant parser behind a writing command

`from_recipe` cannot fail. Missing keys keep their defaults, unknown keys are ignored, a
malformed value keeps the default for that key. That is deliberate and correct: it is what
lets a recipe written today load after the generator gains a parameter.

Behind a command that **writes a file**, the same tolerance is a trap. Point
`asset.texture` at a text file, a PNG, or an empty buffer and it would cheerfully bake the
**default** texture over the destination and report success. Nothing in the tolerant
parser is wrong; the missing piece was that the caller had no way to ask whether the text
was a recipe at all.

```cpp
TextureParams from_recipe(const std::string& text, int* applied = nullptr);
```

It reports how many keys it recognised, the command refuses zero, and the tolerance stays
where it belongs.

Two shapes of junk are tested, because they fail in different places:

| input | where it fails |
|---|---|
| `hi\nthere\n` | no `=`, so the key chain is never reached |
| `colour=blue\nname=pond\n` | reaches the chain and matches nothing |

Testing only the first left the unknown-key branch **deletable with every test still
green** — which is exactly what the mutation run said, before the second case existed.

## Drawing water with a noise generator

The Texture Lab makes seamless procedural textures. Kenney's tiles are **flat colour**.
The first candidates were fractal-noise blues, and beside a solid green grass tile they
read as a different game.

What works is the threshold. One octave of value-basis noise, thresholded high: about
seven eighths of the tile stays one flat blue and the rest lifts to a lighter one. Two
colours, no gradient, short horizontal dashes — the way water has been drawn in pixel art
for thirty years, and the way a procedural generator can join a hand-drawn pack without
pretending to be one.

`seed 7`, `op_amount 0.76`. Both were chosen by rendering a sweep **as a pond ringed with
the pack's own grass** and looking at it, then reading the pixel histogram to confirm
exactly two colours came out. A tile judged on its own is a tile judged in the wrong
context.

One thing the eye caught that no test could have: the first committed recipe had
**hand-computed colour constants**. The sweep used values a script computed; the final
file used values I computed in my head, and `lo=4283022499` is not `0xFF2B5CA3`. The
generator did exactly what it was told and produced a teal-and-purple tile. One histogram
found it.

## The mutation that pointed at a hole in the tests

`draw_tile` had a range check. Mutation removed it and every test stayed green.

The reason is worth stating plainly: **no file in the repository has a wrong tile index**,
so nothing ever reached the guard. It was correct, necessary, and completely unexercised —
the state a guard is in right before somebody deletes it as dead code.

Two changes came out of it, and only one of them is the fix.

The first is a simplification. `Tileset` already answers "no such tile" with a null
sprite, and `sheet_of` already turns a sheet that would not load into an empty one. So the
range arithmetic here was Tileset's job written out a second time:

```cpp
const gfx::Sprite s = sheet_of(a->sheet).sprite(static_cast<std::size_t>(a->index));
if (s.w == 0) return false;
```

One condition, three reasons — a sheet nobody declared, a sheet whose image is missing, an
index past the end of a real sheet — all arriving as the same `w == 0`. Same lesson as
chapter 121, one layer up.

The actual fix is a test. It builds a **second scene on a copy of the asset tree**, whose
theme points grass past the end of a 132-tile sheet:

```cpp
"tile ground 1 town  9999\n"      // grass: past the end -> flat colour
"tile ground 2 town  40\n"        // path:  still real art
```

Grass falls back, the path keeps its art, and the per-tile fallback is now shown to
survive an **authoring mistake** and not merely a missing licence — which is the case that
will actually happen, because a theme is hand-written text.

On a *copy*, because a test that edits the project's own `theme.def` to prove a point is a
test that can lose the project.

## The same test, one expectation inverted

Chapter 121:

```cpp
CHECK(water > 0);         // the pond is still flat: the pack has no water tile
```

Chapter 122:

```cpp
CHECK(flat_water == 0);   // the last id to still be a rectangle
CHECK(deep   > 0);        // the drawn tile's base blue
CHECK(ripple > 0);        // ...and its highlight, an eighth of the tile
CHECK(deep > ripple);
```

That inversion is the whole slice, and it is why the previous chapter left the pond flat
rather than apologising for it: **a stated absence is a test that can later be turned
around.**

The highlight is counted separately on purpose. It is one eighth of the tile — the half of
the art a "close enough" tile would lose. Counting only the base colour would pass just as
happily on a flat blue square, which is precisely what this replaced.

## What was checked

| gate | result |
|---|---|
| `ctest` | 74/74 |
| mutations | 11 run, 11 killed — 2 of them only after a test was written for them |
| web build | links; `farm_water.hrt` and its recipe in the preload; no `saves/`, `releases/`, `channels/` |
| golden path | inspect → package → publish → verify (exit 0) → hub, all green |
| release id | `da5736a151989631` → `184db301032118f2` — the manifest gained a tileset |

The two mutations that needed work first:

| mutation | why it survived | what killed it |
|---|---|---|
| unknown recipe key not counted | the junk fixture had no `=` at all | a second fixture that reaches the key chain |
| the sheet range check deleted | no wrong index exists in the repo | a scene on a copied asset tree with one |

## Ceilings

- **No autotiling.** The pond is a hard-edged rectangle: no shoreline, no transition
  tiles. `autotile_index` has existed since chapter 110 and still has no consumer. This is
  now the most visible unfinished thing in the frame, exactly as the flat pond was.
- **One tile, repeated.** The pond is one 16×16 image tiled, so the ripple pattern repeats
  every tile and reads as a grid at a glance. Variants would need either several tiles and
  a per-cell choice, or the animation the Lab can already make.
- **The water does not move.** `studio::make_sheet` turns any tileable texture into an
  N-frame scroll for free, and nothing in the farm's draw path knows about frames. That is
  the cheapest visible upgrade available and it is not taken.
- **The Texture Lab cannot draw a tile.** It generates. Anything with a *shape* — a
  bucket, a fence post, a face — is out of reach, so the "clone and redraw" half of the
  decision is proven for textures only. A pixel editor as a third Studio workspace is the
  real answer and is not built.
- **The recipe is not in the manifest.** It is source, like the imported PNG, so the
  resource closure does not carry it. Packaging the project therefore ships the pixels and
  not the means to regenerate them.
- **Still nothing measured.** Two sheets are cut at load instead of one; no frame cost was
  taken before or after, and `--bench-ui` still does not run the farm.
