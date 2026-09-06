# Chapter 127 — A colour the sheet did not have

> Code: `src/engine/paint/colour.{hpp,cpp}` · `src/games/studio_shell/pixel_workspace.{hpp,cpp}` ·
> `src/engine/ui/ui.cpp` · `src/games/studio_shell/palette.cpp` ·
> Tests: `tests/test_paint.cpp`, `tests/test_pixel_workspace.cpp`, `tests/test_shell_golden.cpp`

## Tóm tắt (VI)

Pixel workspace (chương 123) có hai cách chọn màu, và **cả hai đều đọc từ file**:
palette là các màu **ảnh đang có** (`build_palette` đếm tần suất), eyedropper là **một
pixel của ảnh**. Cả hai đều là mặc định đúng — khớp màu với ô bên cạnh là phần lớn công
việc pixel art — và hệ quả là editor **không thể tạo ra một sắc mới nào**. Đó là công
cụ *chỉnh sửa*, không phải công cụ *vẽ*. Cùng với "không tạo được file mới", đây là hai
trần đã đẩy chương 125 phải đi cửa `.pix`.

Chương này mở **hai cửa**, vì chúng trả lời hai câu hỏi khác nhau:

- **HSV (ba slider)** — "đậm hơn một chút, vẫn màu đó". Một sắc độ là **một trục** trong
  HSV và **ba trục tương quan** trong RGB. Đây là cửa *mò mẫm*.
- **Hex (một ô nhập)** — "#8B5A2B, đúng màu pack đang dùng". Slider là thao tác kéo
  từng pixel, không ai *nói* cho nó một bộ ba chính xác được. Đây là cửa *chính xác*.

Ba thứ đáng kể hơn cả tính năng:

1. **Trạng thái của mixer KHÔNG phải là màu.** Nếu mỗi frame lại suy ba slider ra từ
   `colour_`, thì kéo Value xuống đáy sẽ **quên cả hue lẫn sat** (mọi màu có v=0 đều là
   đen), kéo lên lại ra **trắng**. Cú kéo thành một chiều — không phải nghĩa của slider.
   Nên mixer giữ `paint::Hsv` riêng, và `colour_` là thứ **suy ra từ nó**.
2. **B, R, G, I vừa là tool vừa là chữ số hex.** Gõ `#8B5A2B` sẽ đổi tool hai lần giữa
   chừng. Guard "ô nhập đang giữ bàn phím thì phím chữ thuộc về nó" lại **đẻ ra một lỗi
   nặng hơn**: `ui::Context` chỉ chuyển focus khi **một widget khác** nhận nó, nên click
   ra canvas *không* trả bàn phím — mọi phím tắt chữ **chết vĩnh viễn** sau một lần gõ.
   Sửa ở `ui::Context::end()`, không phải ở workspace: click ra ngoài ô nhập là gì thì
   ở đâu cũng vậy.
3. **Panel hết chỗ thì control KHÔNG tràn — nó biến mất.** `ui::slot()` **cắt** kích
   thước xuống phần còn lại, nên control cuối nhận rect cao 0: vẽ không ra gì, bấm
   không trúng, và **không ai báo**. Mixer làm inspector cao thêm ~130px, đúng thay đổi
   để lộ chuyện đó. Nay workspace **hỏi chiều cao và so với cái nhận về**, rồi nói ra ở
   **status bar** — bên ngoài panel, vì khi panel quá ngắn thì trong panel không còn chỗ
   để nói.

Round-trip HSV được xác minh **chính xác tuyệt đối trên cả 16.777.216 màu** (0 sai lệch,
76 s ở bản debug). Test trong CI chạy một lát cắt 65.536 màu; con số đầy đủ nằm ở đây.

---

## Both doors read the file

`PixelWorkspace` shipped with two ways to choose a colour, and it is worth being precise
about why neither one could ever produce a new one:

```cpp
// build_palette(): the image's own most-used colours, ranked
std::map<gfx::Color, int> counts;
for (gfx::Color c : img_.pixels) ++counts[c];
```

```cpp
// Tool::Pick: a pixel of the image
colour_ = img_.pixels[hover_y_ * img_.w + hover_x_];
```

Both are the right default. Chapter 123 argued it and the argument still holds: a fixed
rainbow is wrong for every sheet, and matching a neighbour by eye from a generic ramp is
the slowest part of pixel art. The palette is *sampled* precisely so that editing a
pack's tiles hands you that pack's own colours.

The consequence was not written down anywhere: the set of colours reachable by the editor
is exactly the set already in the file. You can rearrange a sheet. You cannot add a
highlight one shade lighter than the one that is there. Chapter 125 needed pieces that no
pack had, and went through the `.pix` door — an ASCII sheet baked offline — rather than
draw them here. Two ceilings pushed it there. This chapter removes one.

## Two doors, because they are two questions

A colour picker is usually presented as one widget. It is two:

| Question | Door | Why the other one fails |
|---|---|---|
| "a bit darker than that, same colour" | three HSV sliders | in RGB a shade is three *correlated* moves; in HSV it is one axis |
| "#8B5A2B — the colour the pack uses" | a hex field | a slider is a pixel-per-step drag; nobody can *tell* it an exact triple |

They live side by side under the sampled palette, and the palette stays first. It is
still the right default; the mixer is for the colour that is not on the sheet yet.

Alpha gets one door, not two. There are three sliders, not four: transparency already has
a dedicated control (swatch 0, and right-drag erases), and the hex field takes
`#AARRGGBB`, so a half-transparent brush is one code away. Four sliders would have cost
another 34 logical pixels of a panel that — see below — did not have them.

## The mixer's state is not the colour

This is the part that looks identical until somebody drags a slider to the bottom.

The obvious implementation keeps one `gfx::Color colour_` and derives `h`, `s`, `v` from
it each frame. It works for hue and saturation. It fails for value, because the map from
colour to HSV is not injective:

```
from_hsv({210, 0.5, 0.0}) == 0xFF000000     // black
to_hsv(0xFF000000)        == {0, 0, 0}      // ...which remembers nothing
```

Drag value to the bottom and the hue slider snaps to 0 and the saturation slider to 0.
Drag it back up and you get **white**. The gesture is one-way — which is not what a
slider means, and the user did not do anything wrong.

So `paint::Hsv` is a type, and the workspace holds one:

```cpp
paint::Hsv   mix_{};
std::uint8_t mix_a_ = 255;
```

with one rule per direction. A slider writes the coordinates and *derives* the colour:

```cpp
if (want_mix_) {
    mix_    = *want_mix_;
    colour_ = paint::from_hsv(mix_, mix_a_);
```

Everything else — a swatch, the eyedropper, a typed code, opening a file — goes through
one function that moves both:

```cpp
void PixelWorkspace::adopt(gfx::Color c) {
    colour_ = c;
    mix_    = paint::to_hsv(c);
    mix_a_  = gfx::a_of(c);
    if (!hex_focused_) hex_field_ = paint::to_hex(c);
}
```

One function, because a selection that left the sliders behind would make the *next* drag
jump to whatever was selected before it — a bug with no visible cause at all.

Alpha rides alongside rather than inside `Hsv`. Dragging hue must not change how
transparent a picked pixel was, and putting `a` in the struct is how it eventually does.

The test is written as the gesture, not as the arithmetic:

```cpp
d.panel(ws, mouse(v.x - 50, my, true, false));        // drag value past the left end
CHECK(ws.colour() == 0xFF000000u);                    // black
d.panel(ws, mouse(v.x + v.w + 50, my, true, false));  // and back past the right end
CHECK(ws.colour() != 0xFFFFFFFFu);
CHECK(ws.colour() == 0xFF80BFFFu);                    // hue 210, sat 0.5, value 1
```

`!= white` and `== the right blue` are the same assertion twice on purpose: the first
names the wrong implementation, the second names the right one.

## Exact, not close

A mixer reads the colour into sliders and writes it back every time one moves. If the
round trip loses one count, a colour drifts a shade every time it is touched, nothing on
screen says so, and the file is the only place the drift is visible — by which time the
sheet is inconsistent. So the claim has to be **equality**, not a tolerance.

It survives because the algebra is exact and only the last step rounds:

```
m       = v - v·s = (mx - d)/255 = mn/255      → round(m·255)  = mn
chroma+m= v       = mx/255                     → round(v·255)  = mx
x + m   = (g-b)/255 + mn/255                   → the third channel, exactly
```

and because `to_byte` rounds instead of truncating — `v·255` for a channel of 200 is
`199.99998` in float, and truncation loses the trip on almost every colour.

The full cube was swept once: **16,777,216 colours, 0 mismatches**. It takes 76 s in a
debug build, so CI runs a 65,536-colour slice of it (every value of r and g, with b
stirred so all 256 of its values appear against varied partners) plus the sextant
landmarks. The full number lives in this chapter and in a comment beside the test,
because a fact that costs 76 s per run to re-derive and only changes when one file
changes belongs in prose, not in the loop.

## B, R, G and I are also hex digits

The editor binds four letters to tools. Three of them are hex digits, and `I` is next to
them on the keyboard. Typing `#8B5A2B` into the field, with the shortcuts live, switches
the tool twice on the way through.

`ui::Context` already exposed the seam for this — `focused()`, documented as being for "a
scene that wants to skip its own shortcuts while a text field has focus" — so the
workspace records whether the field has the keyboard while it draws, and stands down:

```cpp
if (!cmd && !hex_focused_) {
    if (in.pressed(platform::Key::B)) tool_ = Tool::Pencil;
```

**And that guard shipped a worse bug than the one it fixed.** `ui::Context` only ever
moved focus when *another widget* took it. A press on empty space — the canvas, which is
where a pixel editor's pointer spends its life — left the field holding the keyboard.
After one visit to the hex field, every letter shortcut in the editor was dead for the
rest of the session. Nothing on screen said why.

The fix is one line, and it belongs in `ui::Context::end()` rather than in this
workspace, because it is not this workspace's rule:

```cpp
// A press that landed on NO widget takes the keyboard back.
if (in_.pressed && hot_ == 0) focused_ = 0;
```

That one line has a second consumer, and it is the reason a change this small still needs
looking around: the **command palette** takes the keyboard when it opens and never asked
for it again. A press on its scrim now clears focus, which would leave it open and deaf
to letters — a state indistinguishable from a hang. So the palette re-takes focus whenever
nothing holds it, which is what "this overlay owns the screen" already meant everywhere
else (`confirm()` has always done it).

Clicking outside a field is how editing ends in every other program on the machine. The
test drives exactly that, and it drives *both* directions — because a guard that never
lifts is the same bug facing the other way, and it is the half nobody writes a test for:

```cpp
d.panel(ws, mouse(20, 20, true, true));      // the canvas: no widget
d.panel(ws, mouse(20, 20, false, false), b_key);
CHECK(ws.tool() == Tool::Pencil);            // the shortcut came back
```

## A panel with no room does not overflow — it disappears

The mixer made the inspector about 130 logical pixels taller. That is the change that
finds the following, which had been true since the layout engine was written:

```cpp
Rect Context::slot(int size) {
    const int avail = remaining();
    if (size > avail) size = avail;          // never hand back a rect outside the area
```

`slot()` **clamps**. Ask for 30 pixels in a panel with 4 left and you get a rect 4 tall;
ask in a panel with none left and you get a rect **0 tall**. A button drawn into it paints
nothing and cannot be hovered, pressed or focused, and the panel above it looks perfectly
normal. This is the chapter-126 bug seen from the other side: not *drawn somewhere it
cannot be pressed*, but **not drawn at all**.

The whole guard is asking for the height and comparing it with what came back:

```cpp
const ui::Rect save_row = ui.slot(30);
inspector_clipped_      = save_row.h < 30;
```

and then saying so on the **status line**, which is outside the panel — when the panel is
too short there is by definition no room inside it to report that the panel is too short.

Scrolling the inspector would fix it properly, and is not in this chapter: it needs a
content height that a one-pass immediate-mode layout does not know until it has finished,
which is a measurement pass or a frame of lag. A control that vanishes silently is the
bug; a control that vanishes and says so is a small window.

## The two that survived were both the header's own promise

Seventeen mutations, one token each, across `colour.cpp`, `pixel_workspace.cpp` and
`ui.cpp`. Fifteen died immediately — including every one that mattered for the round
trip, both halves of the focus guard, and the clipping report. **Two survived, and they
were the same kind of thing twice: a sentence in the header that no assertion was
checking.**

`paint::Hsv` says *"Hue in degrees [0,360)"*. Delete the wrap at the end of `to_hsv` and
every test still passes — because `from_hsv` normalises whatever it is handed, so the
round trip is perfect with a hue of −30. The place it is *not* perfect is the one that
never appears in a unit test: the hue **slider** has a range of 0…360, so a colour at 330
would park its knob hard against the left end and print `hue: -30.10` above it. The
mutation is only visible in the panel.

Delete the `fmod` at the top of `from_hsv` and, again, nothing fails. `% 6` on the sextant
already gives periodicity for any positive hue, and the `+= 360` beneath handles one
negative turn — so the `fmod` is load-bearing only past **−360°**, which no slider can
produce and no assertion had asked for.

Both are now checked as what they are: the contract, not the use.

```cpp
if (h.h < 0.0f || h.h >= 360.0f) ++out_of_range;               // over the whole sweep
CHECK(std::abs(paint::to_hsv(0xFFFF0080u).h - 330.0f) < 0.5f); // the negative case
CHECK(paint::from_hsv({-400.0f, 1, 1}) == paint::from_hsv({320.0f, 1, 1}));
```

The lesson is narrower than "test more". Both survivors sat in the gap between *what the
function promises* and *what today's caller happens to need*. A guard written for the
promise is untested by every caller who stays inside the range — and it stops being
redundant the day a second caller arrives.

## What was checked

| Claim | How |
|---|---|
| HSV round trip is exact | 65,536-colour slice in CI; full 16,777,216 sweep run once, 0 mismatches |
| hue/sat survive a trip through black | driven as a slider drag, asserting `== 0xFF80BFFF` and `!= white` |
| alpha is carried, not converted | pick a `#80……` colour, drag hue, alpha still `0x80` |
| everything `to_hex` prints, `parse_hex` reads | 4,096 colours round-tripped through the text |
| half-typed input changes nothing | `#FF3C` leaves the colour and the field alone |
| a shorter spelling is not rewritten | `3C7A2E` selects the colour and stays six characters |
| the ceiling is actually gone | a typed colour is painted, saved, and read back from the file — and is in neither the palette nor the original image |
| every door moves the mixer | after a typed code, `from_hsv(ws.mix())` is that colour |
| the field owns the letters | `B` with the field focused types a `B` and does not switch tools |
| ...and gives them back | a press on the canvas restores the shortcut |
| the inspector reports being clipped | drawn at 620 and at 240; `status()` says so only at 240 |
| ...and the Studio's own Pixels tab fits | `test_shell_golden` clicks the tab at 1280×720 (fits) and 900×560 (does not) |
| hue stays inside [0,360) | asserted over the sweep, plus the red-magenta case that goes negative |
| the palette keeps the keyboard | a press on its scrim, then typing `zzzz`: the card must shrink to "no command matches" |
| 17 mutations | 15 killed on the first pass; the 2 survivors were untested header contracts, now closed |

## Ceilings

- **You still cannot create a file.** The workspace edits the textures the manifest
  declares. Drawing a *new* tile means adding an `asset texture` line and putting a blank
  `.hrt` beside it first. This is the second of the two ceilings that sent chapter 125
  through the `.pix` door, and the only one left between "there is an editor" and "new art
  is drawn in the Studio".
- **No 2D shade square.** Three sliders, not a saturation/value plane with a hue strip.
  The plane is one gesture instead of two and would be *more* compact; it also needs its
  own drag handling, because `ui::hit` reports a click and not a drag. Sliders were
  already built, tested, keyboard-reachable and focusable.
- **Mixed colours have no home.** They are not appended to the palette. Once painted, the
  eyedropper recalls them — it is the recall gesture the editor already had — so a colour
  is only lost if it is mixed twice without being used. A "recent colours" row is the fix
  when somebody actually loses one.
- **No `pixel.colour` command.** Choosing a colour is a pointer gesture, not an operation
  with an audit trail, so it does not get a Cmd+K entry. The D-rule is about operations
  that exist in two triggers; this one has one.
- **The inspector does not scroll.** See above. It says when it is clipped; it does not
  fix it.
- **Hue resolution.** The hue slider spans 360 degrees across roughly 240 logical pixels,
  so a drag cannot address every degree. The hex field is the exact door; the slider is
  not meant to be one.
