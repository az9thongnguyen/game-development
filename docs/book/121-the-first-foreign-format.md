# Chapter 121 — The first foreign format

> Code: `src/engine/compress/inflate.{hpp,cpp}` (new) ·
> `src/engine/image_png.{hpp,cpp}` (new) ·
> `src/engine/commands/{asset_commands,all_commands}.{hpp,cpp}` (new) ·
> `src/engine/tilemap/tileset.{hpp,cpp}` (new) ·
> `src/games/farm/theme.{hpp,cpp}` (new) · `src/games/farm/farm_scene.{hpp,cpp}` ·
> Data: `assets/textures/kenney_tiny_town.png`, `assets/textures/town.hrt`,
> `assets/farm/theme.def`, `assets/ATTRIBUTION.md`, `assets/projects/farm.gameproject` ·
> Tests: `tests/test_inflate.cpp` (new), `tests/test_png.cpp` (new),
> `tests/test_tilemap.cpp`, `tests/test_farm.cpp`, `tests/test_farm_scene.cpp`

## Tóm tắt (VI)

Anh chốt: **hỗ trợ cả hai** — trước mắt dùng pack pixel-art **CC0/CC-BY** cho đẹp, sau
đó **clone vẽ lại trong Studio**. Câu đó quyết định kiến trúc, không chỉ quyết định
nguồn art: hai nguồn phải **về cùng một định dạng**, nếu không thì mỗi thứ downstream
(asset cache, resource closure, package hash) phải biết hai loại file.

Nên cánh cửa mở **một lần, và mở vào trong**:

```
PNG (bất kỳ pack nào)  ──asset.import──▶  .hrt  ──▶  engine
Texture Lab (.hrt)     ─────────────────▶  .hrt  ──▶  engine
```

**Luật của repo: SDL2 là dependency runtime duy nhất.** Nên đọc PNG nghĩa là **tự viết
DEFLATE**. `inflate_core` là RFC 1951 + 1950: khối stored / fixed / dynamic Huffman,
back-reference LZ77, Adler-32 **có kiểm**. Chỉ giải nén, **không nén** — không thứ gì ở
đây ghi PNG, và một compressor là bài toán lớn hơn nhiều mà **chưa ai cần**.

**Test là các stream do zlib thật tạo ra**, ở nhiều level cố ý để **cả ba loại khối**
đều xuất hiện. Một decoder chỉ từng thấy một loại là decoder chạy được cho tới khi gặp
file của người khác.

**Farm giờ có art**: Kenney Tiny Town (CC0, 16px, 12×11 tile). Map vẫn giữ **id ngữ
nghĩa** (ground 1 = cỏ), và `farm/theme.def` là chỗ nối id ↔ chỉ số tile. **Id không có
dòng nào thì không có art** — và rơi về đúng màu phẳng cũ. Đó là cách "hỗ trợ cả hai"
hoạt động **theo từng ô**: Tiny Town **không có tile nước**, nên cái ao vẫn là màu
phẳng trong khi mọi thứ quanh nó đã có art. Vẽ một tile nước trong Texture Lab, thêm
một dòng, nó xuất hiện — **không cần build lại**.

**Hai mutation sống sót lúc đầu và dẫn tới chỗ tốt:** `draw_tile` có hai guard **che
lẫn nhau** (xoá cái nào test cũng xanh), và một guard thứ ba lặp lại bất biến mà
`parse_theme` đã giữ. Giờ còn **một điều kiện, hai lý do**.

---

## Why the door had to open

Chapter 24 chose `.hrt`: magic, width, height, raw RGBA. Ten lines to read, no
dependency, and it has never been a mystery. `docs/new-plan` recorded the decision
that follows from it (D5): the pixel editor uses `.hrt`, **not** PNG, because the repo
has neither a PNG encoder nor a decoder — and PNG waits until there is a real need to
exchange with the outside world.

This is that need, and it arrived with a shape: *support both sources*. An
open-licence pack now, art redrawn in the Studio later. Two sources that stay two
formats would mean the asset cache, the manifest's resource closure and the package
hash each learning about a second kind of file, forever. Two sources that converge on
one format cost one import step, once.

So the door opens **inward only**. `decode_png` exists; there is no `encode_png`.
Nothing here writes a PNG — art leaves as `.hrt` — and an encoder needs a compressor,
which is match-finding and Huffman construction with no consumer in this repository.
Half a codec, where the half is the one that is needed.

## DEFLATE, by hand

A PNG's pixels are a zlib stream, so the first thing to write was not an image reader.

Two conventions live at the bottom of it, and getting either backwards produces a
stream that decodes for a while and *then* explodes — the least helpful failure mode
available. DEFLATE reads bits LSB-first within a byte; Huffman codes are stored with
their bits in the other order. Both live in one small `Bits` class, named.

The Huffman decoder is built from code **lengths** alone, because that is all the
format transmits: sort by (length, symbol), hand out consecutive codes, and walk one
bit at a time comparing against the first code of each length. A lookup table would be
faster and this is not on a frame path. The check that matters is not speed:

```cpp
int left = 1;
for (int len = 1; len <= 15; ++len) {
    left <<= 1;
    left -= count[len];
    if (left < 0) return false;      // over-subscribed: a corrupt table
}
```

The fixed tables of §3.2.6 are *built from their code lengths* rather than written
out, so a typo in a 288-entry literal table cannot exist.

And one line carries most of the safety of the whole file:

```cpp
if (distance > out.size()) return fail(why, "distance points before the output");
```

A back-reference reaching before the start of the output is the difference between a
corrupt file and reading somebody else's memory.

### Testing a decompressor

There is only one test worth having: bytes a **genuine compressor** emitted, decoded
back to the bytes that went in. The fixtures were produced by a real zlib at levels
chosen so that all three block types appear — level 0 forces STORED, a short input
gets FIXED Huffman, a long skewed text gets DYNAMIC tables. A decoder that has only
ever seen one of the three works until someone else's PNG arrives.

The refusals are the other half:

| corruption | what catches it |
|---|---|
| a flipped byte mid-stream | Huffman, or failing that the Adler-32 |
| truncation | the bit reader runs out |
| a checksum that does not match valid data | Adler-32, by name |
| block type 3 | the decoder itself |

The checksum is verified rather than skipped, because the caller above this is an
image reader, and the symptom of "decoded anyway" is wrong colours nobody can explain.

## PNG, narrowly

Supported: bit depth 8, colour types 0/2/3/6, `tRNS`, all five scanline filters,
non-interlaced. That is what real packs contain. Everything else is refused **by
name** — `"interlaced PNG not supported"`, `"only 8-bit channels supported"` — rather
than producing an image that is subtly wrong.

Chunk CRCs are checked. `tRNS` on a palette image may be *shorter* than the palette,
and entries past its end are opaque; that is easy to read as out-of-range and get
wrong, so it is a fixture.

Three fixtures are hand-built in the test so their pixels are known **by
construction** rather than by asking another decoder, and built to be awkward on
purpose: one row per scanline filter, a `tRNS` shorter than its palette, greyscale.
The fourth is a real Kenney tile and the fifth is the 5 KB sheet this repository now
ships, read from the asset tree — so if the import source is ever replaced, the test
notices.

Decoding a fixture proves the fixture. Decoding somebody else's file proves the
decoder.

## The import is offline

```sh
./build/demo --cmd asset.import textures/kenney_tiny_town.png textures/town.hrt
# imported textures/kenney_tiny_town.png -> textures/town.hrt  (192x176, 135180 bytes)
```

The engine does not decode PNG at runtime. A game ships `.hrt`; the PNG reader exists
for the moment a human brings something in. The command refuses a destination that
does not end in `.hrt`, because the extension is not decoration here — it is what
tells the rest of the project the file is readable at runtime, and PNG bytes under an
`.hrt` name would pass every later check and fail at load.

Adding a second family of commands would have made **eight** call sites of
`register_*_commands`, with nothing stopping a process from registering one family and
not the other — so `--cmd` and the Studio's palette would list different things
depending on which flag started the process. One `cmd::register_all` now. Same lesson
as the chapter before it, one layer down.

## Semantic ids, and a theme

The map stores **semantic** ids: ground 1 is grass, ground 2 is the path, decor 1 is a
tree. That is what an author edits and what the Studio's palette shows. Storing sheet
indices instead would mean renumbering every level when the art changes, which is how
a level stops being editable.

`assets/farm/theme.def` is the join:

```
sheet textures/town.hrt 16

tile ground 1 0     # plain grass
tile ground 2 40    # bare dirt, the worn path
tile decor 1 28     # a small green tree
```

Data, not code, for the reason the crop table is a text file (D27): art is iterative
work and iterative work should not need a compiler.

**An id with no line has no art**, and the caller falls back to the flat colour it drew
before. That is what makes "support both" work *per tile* rather than all-or-nothing —
and the farm demonstrates it in the only place it could: Tiny Town has no water tile,
so the pond is still painted flat while everything around it is themed. Draw one in the
Texture Lab, add a line, and it appears.

The test says exactly that, in pixels:

```cpp
CHECK(grass == 0);        // the old flat colours are gone...
CHECK(path  == 0);
CHECK(tree  == 0);
CHECK(water > 0);         // ...except the one the pack has no tile for
```

The indices were **read off the sheet, not guessed**: the first pass had the stones one
tile to the left, which rendered as dirt in the field and looked entirely plausible.

## Two mutations that survived, and were right to

`draw_tile` started with three guards:

```cpp
if (!theme_ || id == 0 || tiles_.count() == 0) return false;
const int index = theme_->index_of(layer, id);
if (index < 0) return false;
const gfx::Sprite s = tiles_.sprite(index);
if (s.w == 0) return false;
```

Deleting the `index < 0` guard: every test still passed, because `-1` cast to
`size_t` is huge, `sprite()` returns a null sprite, and the `s.w == 0` guard catches
it. Deleting the `s.w == 0` guard: every test still passed, because `index < 0` caught
it first. **Each masked the other**, so the suite could not tell which one was doing
the work — and a real edit could delete either.

Deleting `id == 0`: passed too, because `parse_theme` refuses to map id 0 at all, so
an empty cell can never have a line.

The fix is not more tests. It is one condition with two reasons, and an invariant that
lives in exactly one place:

```cpp
if (!theme_ || tiles_.count() == 0) return false;
const int index = theme_->index_of(layer, static_cast<int>(id));
if (index < 0 || static_cast<std::size_t>(index) >= tiles_.count()) return false;
```

Now deleting it fails the pond test. Redundant guards are not free: they are places a
mutation can hide.

## Testing notes

**9 mutations, 7 killed on the first pass**, and the two survivors are the section
above — they were a design problem, not a testing one.

| mutation | what it broke |
|---|---|
| partial cells padded instead of dropped | a half tile treated as a tile |
| the cut reads the wrong column | every tile is a slice of its neighbour |
| the Paeth filter treated as Sub | one row in five decodes wrong |
| `tRNS` ignored | a palette image comes back fully opaque |
| chunk CRCs not checked | a damaged file decodes into rubbish |
| an unmapped id draws whatever the cast lands on | the pond gets a random tile |

The tileset test fills each source cell with its own index as a colour, so a mis-cut is
a wrong colour rather than a plausible picture — and checks **every pixel** of each
cell, because a cut that reads the wrong row still starts correctly.

## Ceilings

- **The import source ships in the web bundle.** `kenney_tiny_town.png` lives in
  `assets/textures/` so that `asset.import` — which reads through `assets::`, like all
  I/O here — can find it, and the preload therefore carries 5 KB the runtime never
  reads. Keeping the source next to its output is what makes the import reproducible
  in one command; that seemed the better trade.
- **`map2` already has a native tileset field** (`TilesetRef`, and a per-layer tileset
  name) that this does not use. That is the eventual home: a map that names its own
  sheet and stores indices directly. It costs a renumbering and the loss of the flat
  fallback, so the theme goes first.
- **No autotiling.** `autotile_index` has existed since chapter 110 and the farm does
  not use it, so grass meets dirt with a hard edge.
- **One sheet, one theme file.** Multiple sheets per map, or per-layer sheets, are not
  expressible.
- **The pond is still a rectangle of flat blue**, by design here, and it is the most
  visible thing in the frame. It is the first thing to draw in the Texture Lab.
- **Nothing was measured.** Cutting 132 tiles copies the sheet once at load; no
  before/after frame cost was taken, and `--bench-ui` does not run the farm.
