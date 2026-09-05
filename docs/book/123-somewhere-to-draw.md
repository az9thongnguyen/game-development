# Chapter 123 — Somewhere to draw

> Code: `src/engine/paint/paint.{hpp,cpp}` (new lib `paint_core`) ·
> `src/games/studio_shell/pixel_workspace.{hpp,cpp}` (new) ·
> `src/games/studio_shell/studio_shell_scene.{hpp,cpp}` · `src/main.cpp` ·
> Tests: `tests/test_paint.cpp` (new), `tests/test_pixel_workspace.cpp` (new),
> `tests/test_shell_golden.cpp`

## Tóm tắt (VI)

Chương này bắt đầu bằng việc **định làm autotile** và phát hiện ra **không làm được** —
vì lý do đáng giá hơn cả việc làm xong.

`autotile_index` (luật blob 47 mảnh) có từ chương 110 và **chưa ai dùng**. Nghe như
đúng việc kế tiếp: cái ao là hình chữ nhật cạnh cứng, cỏ giáp đất là cạnh cứng. Nhưng
trước khi viết, tôi **nhìn sheet**:

- Tiny Town có **9 mảnh** đất-trên-cỏ (một khối 3×3), **không phải 47**.
- 9 mảnh **không thể** vẽ một dải **rộng 1 ô** — nó chỉ có góc lồi.
- Và đường đi của farm **rộng đúng 1 ô** (`assets/maps/farm_home.map2`).
- Cái ao thì là khối 5×4 — 9 mảnh sẽ hợp — nhưng nước là art *của mình*, và mình mới
  có **đúng một tile**.

**Chặn ở ART, không phải ở code.** Luật autotile không dùng được vì **không bộ art nào
trong tay có đủ mảnh**, và Texture Lab **không sinh ra hình có hình dạng** — mọi pixel
nó tạo là hàm của mười hai con số. Phải có người cầm con trỏ. Và **cho tới file này,
trong dự án không có chỗ nào để làm việc đó.**

Nên chương này là **workspace thứ ba**: một trình sửa pixel, sửa `.hrt` — định dạng
raster duy nhất engine đọc, nên tile vẽ ở đây và tile import từ pack là **cùng một loại
file** khi xuống tới renderer. Đúng tính chất mà chương 121 và 122 tồn tại để giữ.

Bốn quyết định đáng nói:

1. **Nhận một DANH SÁCH texture, không phải một đường dẫn.** Nếu chỉ mở cái đầu tiên,
   editor của farm sẽ mở đúng file Kenney — file mà pixel của mình **không được** dính
   vào (chương 122).
2. **Palette lấy TỪ chính ảnh**, nhiều nhất trước, ô 0 là tẩy. Bảng màu cố định thì sai
   với mọi sheet: dò màu bằng mắt là phần chậm nhất của pixel art. Và nó **ít code hơn**
   một colour picker, không phải nhiều hơn.
3. **Đổi texture bị TỪ CHỐI khi chưa lưu.** Không có undo xuyên qua reload, nên lựa chọn
   còn lại là mất im lặng. Đi qua `open_index`, mà **cả** list trong inspector **lẫn**
   lệnh `pixel.next` đều gọi — một thao tác không được phép chỉ tồn tại ở một cửa.
4. **Canvas vẽ ô caro rồi BLEND ảnh lên.** Nếu copy đè, pixel trong suốt và pixel đen
   trông giống hệt nhau — đúng cái lỗi khiến sprite ship ra với viền đen.

Test ghim **vòng tròn đầy đủ**: vẽ → undo qua **hai** màu cũ khác nhau → lưu → **đọc lại
FILE và decode**. Một editor vẽ đẹp mà ghi ra byte engine không đọc được thì **tệ hơn là
không có editor**.

---

## The chapter that started by not being written

The plan said autotiling. `autotile_index` has existed since chapter 110, it has never
had a consumer, and a core with no consumer is exactly what this project's own rules
call motion without connection. The pond is a hard-edged rectangle. Grass meets dirt
with a straight seam. It looked like the obvious next thing.

So the first step was to look at the sheet, and the sheet said no.

| what the rule needs | what Tiny Town ships |
|---|---|
| 47 blob pieces | a **9-piece** dirt patch (a 3×3 block) |
| pieces for a 1-wide strip | none — a 9-patch has convex corners only |

And the farm's path, read straight out of `assets/maps/farm_home.map2`, is one tile
wide for its whole length. The pond *is* a 5×4 block that a 9-patch would handle
beautifully — but the pond is our own art and we have exactly one water tile.

**The blocker is art, not code.** No amount of noise makes a narrow-strip piece: the
Texture Lab generates, and everything it can produce is a function of twelve numbers,
which is why chapter 122 could draw water and could not have drawn a bucket. Somebody
has to move a cursor over individual pixels, and until this chapter there was nowhere in
this project to do that.

That is worth more than a finished autotiler. The rule was not wrong and the code was
not missing; the dependency ran the other way round from how the roadmap had it.

## `paint_core`, deliberately the same shape as `map_edit`

`command_stack.hpp` has said since chapter 113 that "a map workspace, a scene workspace
and a pixel editor all share one history implementation". This is the third one arriving,
and it is the same shape as `mapedit::` on purpose, because it is the same problem: an
edit **returns a `doc::Command`** rather than mutating and hoping somebody records it.

A stroke accumulates in `Stroke` rather than merging on the stack, for exactly the reason
`map_edit.hpp` gives — the stack's merge keeps the FIRST revert and the LATEST apply,
which is right for a gesture whose latest state subsumes every earlier one and wrong for
one that accumulates. Merging a painted line would restore only its first pixel on undo.

One thing a map editor does not need:

```cpp
void touch_line(int x0, int y0, int x1, int y1);
```

A map is edited at zoom 2, where the pointer rarely crosses two cells in a frame. A pixel
editor is used at zoom 8, where an ordinary gesture crosses several — and touching only
the current pixel draws a **dotted line**, which is the single most obvious way this kind
of tool can feel broken. Bresenham, integer only: stepping a float and rounding puts the
diagonal half a pixel off at some slopes, and here half a pixel is the whole unit of work.

### The tests are about reversibility, not about pixels changing

"Painting changes pixels" is not worth asserting. What is worth asserting is that every
edit restores **byte for byte**, and the fixture is built so that a stroke crosses two
different previous colours:

```cpp
CHECK(cmd::run("pixel.undo").ok);
CHECK(at(ws.image(), 0, 0) == kOdd);   // the one odd pixel the drag started on
CHECK(at(ws.image(), 1, 0) == kBg);    // ...and the ordinary ones beside it
```

An undo that repaints one uniform "previous colour" passes a weaker test and loses work
the first time somebody paints over two colours at once. An editor whose undo is
approximately right is worse than one with no undo, because the first one teaches you to
trust it.

Fill matches on the **whole colour including alpha**. Comparing RGB would leak across the
boundary between a transparent area and a black one and quietly make it opaque — and a
sheet full of transparent margins is exactly what an imported pack is.

## The workspace, and four decisions

Chapter 116 drew the `Workspace` interface from two examples rather than one, precisely so
it would not be a shape moulded around its only occupant. This is the first test of
whether it fits a third, and it did, with nothing added: `inspector_width()` was already a
request rather than a constant, and this asks for 280.

**It takes a list of textures, not a path.** If it opened the first texture the manifest
declares, the farm's editor would open `textures/town.hrt` — Kenney's imported sheet, the
one file our own pixels must never end up in, for the two reasons chapter 122 gave
(attribution, and re-import destroying the work). A list makes the *other* sheet reachable.

**The palette is sampled from the image.** Most-used colours first, the eraser always at
swatch 0:

```cpp
std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    return a.first != b.first ? a.first > b.first : a.second < b.second;
});
```

A fixed ramp would be wrong for every sheet. Matching a colour by eye is the slowest part
of pixel art, and editing a pack's tiles with a generic rainbow means every stroke is
visibly foreign. It is also **less** code than a colour picker, not more. Ties break on
the colour value rather than on map order so the palette is identical on every machine —
a swatch that moves between runs is a swatch nobody learns.

**Switching texture is refused while dirty.** There is no undo across a reload, so the
alternative is losing work silently. It goes through `open_index`, which both the
inspector's list and the new `pixel.next` command call, because an operation must not
exist in only one trigger — the same rule the command registry exists to enforce.

**The canvas draws a checkerboard and blends the image over it.** Copying instead would
make a transparent pixel and a dark pixel look identical on screen, which is exactly the
mistake that ships a sprite with a black halo nobody noticed until it was on a light
background.

## The round trip is the claim

```cpp
CHECK(cmd::run("pixel.save").ok);
const auto on_disk = read_image(kPath);
CHECK(on_disk->pixels == ws.image().pixels);
```

Everything else in the test could pass while the editor writes bytes the engine cannot
read, and that failure surfaces later — as a game that will not start, long after the
session where the art was drawn. So the file is read back and **decoded**, not just
written.

The last case opens Kenney's real 192×176 sheet instead of a fixture. A fixture proves the
fixture; the sheet is the file a user would actually open first, and it is where the
sampled palette hits its cap, the tile guide has something to guide, and the layout has to
survive an image far larger than the canvas.

## A test that was right for the wrong reason

`test_shell_golden` clicked the Scene tab at "3/4 across the tab row". With two tabs that
is the middle of the second one. With three tabs it is the middle of the **third**, and
the day this workspace was added, ten assertions failed at once in a file that had nothing
to do with pixels.

It now computes the centre of tab *i* of *n*. The fraction was not wrong; it was a
coincidence written down as if it were a coordinate.

## The bug the browser found

The editor's keyboard shortcuts did nothing in a headless Chrome. Neither did Cmd+Z.
The cause is not in the editor, and it is four years older than this chapter in spirit:

**SDL2's Emscripten backend listens for key events on the canvas element.** The page
gave the canvas `tabindex="-1"` and never focused it, so `document.activeElement` was
BODY, and every keydown went to the body, bubbled up to document and window, and was
never seen by SDL. A DOM probe said it exactly:

```
listeners saw: ["window:r", "document:r"]        // and nothing on the canvas
```

That was true of **every web build since chapter 118**. No WASD in the farm, no F5/F9,
no chess input, no Studio shortcuts — none of it, ever. It went unnoticed because every
browser check so far had used the **mouse**, which SDL registers on the canvas the page
hands it in `Module.canvas` and which therefore always worked. Chapters 119 and 122 both
proved a click; neither pressed a key.

The fix is two halves: `tabindex="0"`, because an element that cannot be focused cannot
be given focus, and `focus()` on runtime init plus on every mousedown — clicking the page
chrome moves focus away and the keyboard would go quiet again with nothing on screen to
say why.

Afterwards, in the browser: pressing `R` switches the tool to Rect, and holding `D` in
the farm walks the player east.

### One harness fact, recorded because the next person will hit it

Key edges are **poll-derived** — `backend_sdl` reads `SDL_GetKeyboardState` once a frame
and diffs against the previous frame. A synthesized press and release inside one 16 ms
frame is therefore invisible, and the first three attempts at this test failed for that
reason and looked exactly like the bug being hunted. A driver has to HOLD the key. That
is a property of the engine, not a defect, but it is a property that makes a broken
keyboard and a too-fast test indistinguishable until you check which one you have.

## What was checked

| gate | result |
|---|---|
| `ctest` | 76/76 (two new suites) |
| mutations | 8 run, 8 killed |
| web build | links; keyboard input works in a browser for the first time (see above) |
| golden path | inspect → publish → verify (exit 0) → hub, all green; release id unchanged |
| command surface | `--cmd` in a terminal lists **no** `pixel.*` — there is no document open in a terminal, and that is the truth rather than an omission |
| browser | the editor runs in WASM: a drag paints, the status shows `* unsaved`, undo is enabled and labelled `draw`; `R` switches tool; the farm's `D` walks the player |

## Ceilings

- **Nothing has been drawn with it yet.** The tool exists; the narrow path pieces that
  motivated it are still not in the sheet, and autotiling still has no art to run on.
  That is the next chapter, and it is now a drawing job rather than a blocked one.
- **One layer, no selection.** No move, no copy/paste, no flip, no per-tile view. A sheet
  is edited as one big image with a 16 px guide drawn over it.
- **No colour picker.** You can only paint colours the image already contains, or
  transparent. Introducing a genuinely new colour is impossible without editing the file
  another way — which is fine for touching up a pack and wrong for drawing from scratch,
  and it is the first thing to add.
- **The tile guide is fixed at 16.** Every sheet in this repository is 16 px; a sheet that
  is not would get a wrong grid with no way to say so.
- **No canvas resize, no new file.** It edits images that already exist.
- **Autosave writes the whole image.** A 135 KB sheet is rewritten every ten seconds while
  dirty. Fine at this size, and it is a full-file write rather than a diff.
