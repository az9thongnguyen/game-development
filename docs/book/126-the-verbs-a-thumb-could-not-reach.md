# Chapter 126 — The verbs a thumb could not reach

> Code: `src/games/farm/controls.{hpp,cpp}` · `src/games/farm/farm_scene.{hpp,cpp}` ·
> Tests: `tests/test_farm.cpp`, `tests/test_farm_scene.cpp`

## Tóm tắt (VI)

Chương 118 cho farm chạy trên trình duyệt; chương 124 cho nó **d-pad + hai nút hành
động**. Từ đó tới nay câu chuyện được kể là "chơi được trên điện thoại". Chương này
đi kiểm tra câu đó và tìm ra **ba lỗ**, xếp theo độ nghiêm trọng tăng dần:

1. **Không đổi được tool.** Bàn phím có 1–4; màn hình không có gì. Tool bạn khởi động
   cùng là tool bạn có mãi mãi → **ba trong bốn động từ của game không với tới được**.
2. **Không lưu được.** F5 là đường duy nhất ghi file. Ngày chơi chỉ sống sót nếu bạn
   ngủ.
3. **Nói chuyện với Anna là ĐÓNG BĂNG VĨNH VIỄN.** Nhánh `talking_` trong `update()`
   **`return` trước khi đọc con trỏ**. Hộp thoại mở ra là hết: không trả lời được,
   không đi được, không lưu được, không thoát được. Lối ra duy nhất là tắt app. Màn
   hình trông **hoàn toàn bình thường** suốt thời gian đó.

Nhánh hội thoại đã bỏ qua con trỏ **từ khi farm ra đời (ch. 113)** — lúc đó vô hại, vì
cả game là bàn phím. Nó thành **khoá cứng** đúng lúc chương 124 dựng d-pad lên và tuyên
bố "chơi được bằng tay". Lý do hai chương không ai thấy rất cụ thể: **`test_farm_scene`
không có test hội thoại nào cả.** Không phải test yếu — là *không có*.

**Câu trả lời cho lỗ 1 không phải "thêm bốn nút".** Hotbar bốn ô đã ở đó từ chương 113,
đã vẽ đúng bốn tool và đúng cái đang cầm. Nó là **bức tranh của một control chưa tồn
tại**. Nên nó *trở thành* control — và hình học của nó **chuyển vào `controls.hpp`**,
nơi mọi hình chữ nhật khác trên màn hình này đã sống, thay vì được **chép** ra. Đó là
đúng luật file đó tự viết ở dòng đầu: *vẽ một nơi, bấm một nơi là con bug vô hình
trong ảnh chụp*.

**`layout()` nhận thêm MỘT bit state — `conflict` — và đó là quyết định, không phải
tiện tay.** Khi cloud có bản lưu khác bản của bạn, chip góc phải nói "F6 keep yours /
F7 take cloud" — hai phím điện thoại không có. Và `sync_saves` chạy lúc **CONNECT**,
nên người mở farm trên máy thứ hai gặp câu hỏi đó **ngay lúc khởi động**, chưa chạm gì.
Nút `save` bị **thay** bằng hai đáp án, chứ không đứng cạnh: `save_game()` gọi thẳng
`push_save()`, nên bấm save lúc đang xung đột **âm thầm có nghĩa "bản của tôi thắng"**
— một câu trả lời cho câu hỏi người ta chưa từng thấy.

**Số liệu:** 76/76 · **20 mutation: 18 chết ngay, 2 sống sót và cả hai cùng một loại —
*được vẽ nhưng đã chết* — thêm hai test thì 20/20** · golden path xanh · web build xanh ·
**đã render và
nhìn** — và chính ảnh chụp tìm ra lỗi thứ tư: nút `v` của d-pad vẽ đè lên chip cảnh báo
config, đúng cái dòng mà comment của nó gọi là "dòng operator phải đọc được từ đầu
phòng". Không test nào, không compiler nào có ý kiến.

## Three holes, and the one that was a freeze

The farm has had on-screen controls since chapter 124: a d-pad and two action buttons,
read from the pointer because SDL synthesizes a mouse from a finger. That was enough to
call it playable by hand, and it was not true. Walking, using and cycling the seed are
three verbs. The game has more:

| verb | keyboard | on screen, before this chapter |
|---|---|---|
| walk | WASD / arrows | d-pad |
| use the held tool | Z / Space | `Z` button |
| cycle the seed | Q | `Q` button |
| **choose a tool** | **1 2 3 4** | **nothing** |
| **save** | **F5** | **nothing** |
| **answer the cloud** | **F6 / F7** | **nothing** |
| **answer an NPC** | **Z, arrows** | **nothing — and it froze** |

The last row is not a missing convenience. `update()` gives the dialogue the input and
returns:

```cpp
if (talking_) {
    ...keys...
    return;
}
```

Everything after that line — the pad, the pointer, the tools, the save, the clock — is
unreachable for as long as the box is up. On a keyboard nobody noticed, because Z closes
it. Without a keyboard, opening the dialogue is a **frozen game whose only exit is
force-quit**, and the screen looks completely normal throughout: the world is drawn, the
d-pad is drawn, the buttons highlight under a finger. They just do nothing.

The dialogue has ignored the pointer since the farm existed (chapter 113), and that was
harmless while the whole game was keyboard-only. It became a **lock** in chapter 124,
which put a d-pad on the screen and called the farm playable by hand. Two chapters, and
the reason nobody noticed is worth writing down plainly: **`test_farm_scene` had no
dialogue test at all.** Not a weak one — none. There was no assertion anywhere that a
conversation could be *finished*.

## The hotbar was a picture of a control that did not exist

The obvious way to add tool buttons is to add four buttons. It is the wrong way, and
the reason is already on screen.

The hotbar has been drawn since the farm existed (chapter 113): four slots, the held one bordered and
brighter, the seed slot carrying what would be planted and how many are left. It is
exactly the four rectangles a player would reach for. It answered nothing.

So it becomes the control, and — this is the part that matters — **its geometry moves
into `controls.hpp` rather than being copied into the hit test.** It had been four
constants inside `render()`:

```cpp
constexpr int kSlotW = 62, kSlotH = 24;
const int     y = H - kSlotH - th::space_sm;
```

Which was fine. A picture may compute its own rectangle. The moment it has to answer a
tap there are two layouts for one strip, and `controls.hpp`'s first paragraph is about
precisely that: *a control that is drawn in one place and hit in another is the bug this
shape exists to prevent, and it is invisible in a screenshot — the button looks right
and does nothing.*

The move brought a real question with it. A d-pad button is 44 pixels because that is
what a finger needs; a hotbar slot is 24 because that is what a **label** needs. Both
numbers are right, and they are right about different things. The resolution is that
the strip has two heights and one condition:

```cpp
int hud_height(int w, int h) { return pad_fits(w, h) ? kBtn : kSlotH; }
```

It becomes a target exactly when the pad fits, because that is the same question —
*is there room for a thumb here* — asked once instead of twice. On the 480×270 retro
framebuffer the hotbar is the 24-pixel strip it has always been, and a mouse can still
click it: 24 pixels is unreachable for a thumb and trivial for a cursor, and dropping
that would be a regression dressed up as consistency.

That created a small circular dependency worth naming, because breaking it the other
way would have been a latent bug: the pad must clear the hotbar, and the hotbar is tall
exactly when the pad is shown. It is broken by reserving the **tall** strip always. The
reserved height is then a constant, and the error runs in the safe direction — the pad
can be hidden on a screen where the short hotbar would have left room, never drawn on
one where the tall hotbar will cover it.

## `layout()` takes one bit of state, on purpose

Everything in `controls.cpp` was pure geometry: a width, a height, rectangles out. It
now takes a third argument.

```cpp
Layout layout(int w, int h, bool conflict);
```

That is a deliberate widening, not a convenience. When the cloud is holding a save that
disagrees with the local one, the chip in the corner stops reporting and starts asking —
`F6 keep yours / F7 take cloud` — and a phone has neither key. It is not a corner case:
`sync_saves()` runs on **connect**, so a player who opens the farm on a second device
meets that question at startup, before touching anything, and can neither answer it nor
save afterwards.

The two answers take the seat above `use`, and `save` is **removed** from that seat
rather than sharing the row with them. That is the load-bearing part. `save_game()`
calls `push_save()` unconditionally, so saving during a conflict silently means *mine
wins* — which is a perfectly reasonable reading of an explicit keypress, and a terrible
thing to offer as a 44-pixel square to somebody who never saw the question. Making the
seat exclusive means the ambiguity cannot be tapped.

Within the pair, `keep` gets the outer seat — the one a thumb reaches without moving the
hand — because it is the answer that changes nothing. `take` discards the play on this
device, so it costs a deliberate stretch. A destructive answer should never be the
comfortable one.

Each button is labelled with the **key it duplicates**: `F5`, `F6`, `F7`, beside the `Z`
and `Q` that were already there. That is not decoration. It makes the two input paths
teach each other — and the chip that has always read `F6 keep yours / F7 take cloud`
becomes, with no change at all, the legend for the two buttons underneath it.

**No `F9`.** Loading discards the day you are holding and the farm has no modal to ask
twice with, so on a surface where the verbs are squares under a thumb the destructive
one stays behind a key you have to mean. That is a ceiling, and it is in the list at the
bottom rather than in a commit message.

## An empty box is the whole guard

Three of the boxes exist only sometimes. The alternative to storing them as empty
rectangles is a flag beside each one — `bool has_save` — and that is two facts about one
thing, which eventually disagree.

`Box::contains` is already false for every point in a zero-width box, so a control that
was not laid out cannot be hit even by code that forgets to ask:

```cpp
if (l.save.contains(p.x, p.y)) a.save = true;
if (l.keep.contains(p.x, p.y)) a.keep = true;
```

Both lines run on both layouts. Only one of them can ever fire, and it is the layout
that decided which. The test that matters is the one pointing at the same pixel on both:

```cpp
a = farm::read(l,  {cl.keep.x + 2, cl.keep.y + 2, true, true});   CHECK(a.save && !a.keep);
a = farm::read(cl, {l.save.x  + 2, l.save.y  + 2, true, true});   CHECK(a.keep && !a.save);
```

A button that is **hit but not drawn** is the same bug as one drawn but not hit, seen
from the other side — and it is the one a screenshot cannot show.

The renderer needs one line of the same rule, and it is **not** redundant: an empty box
defaults to `{0,0,0,0}`, and the label is centred *in* the box, so without the guard the
glyph "F6" would be drawn at roughly `(-8, -6)` — in the corner of the screen, outside a
conflict, for no reason.

## A modal needs no new buttons

The freeze is fixed by the same move, one file over. The dialogue panel's geometry was
four expressions inside `render()`; it joins everything else that answers a tap:

```cpp
Talk talk_layout(int w, int h, int choices);
TalkAction read(const Talk& t, const Pointer& p);
```

Tap an option to pick it; tap the panel to finish the line and move on. That is what a
text box on a phone has meant for fifteen years, it costs nothing on the desktop where
the keys still work, and it adds **no button anywhere** — the modal is its own control
surface.

Three details are load-bearing.

**Options are tested before the panel.** Every option is *inside* the panel, so an
implementation that tested the panel first would swallow every answer and turn the whole
box into one "next" button that picks whatever the keyboard cursor last highlighted. A
first draft did exactly that.

**A tap moves the cursor before choosing.** Otherwise the row that lights up and the row
that happens are different rows.

**There is no cap on the option count.** The first cut stored `Box choice[6]` and
documented six as a ceiling. Six is more than `anna.dlg` offers and a ceiling nobody
meets today is a ceiling nobody *sees* when they finally do — so the rows are derived
from a first row and a pitch, any count lays out, and a panel that would not fit lays
out nothing at all and leaves the keyboard as the honest answer. The same rule the pad
has followed since chapter 124.

The panel also moved **above** the hotbar rather than sitting a fixed distance from the
bottom edge. It used to be `h - box_h - 12`, which cleared a 24-pixel strip and covers a
44-pixel one — a number that was right about a layout it never asked. The tool and the
seed count are exactly what a player checks while an NPC is explaining what grows here.

And the pad is **not drawn** while the box is up, because `update()` returns before
reading it. A control that is drawn and does nothing is this file's founding bug,
arrived from the other direction.

## The screenshot found the fourth hole

Everything above was green — 76 tests, twenty mutations — and then the picture showed
the d-pad's `v` button drawn straight through the middle of the config-problem chip.

That chip is the line an operator's typo in remote config arrives on. Its own comment
in `render()` calls it *"the one line an operator needs to be able to read from across
the room"*. It is left-aligned at `x = 8`; the d-pad is in the bottom-left corner. They
had never met, because the chip was anchored a fixed distance above a 24-pixel hotbar
and the pad cleared that. Move the hotbar to 44 and the chip moves up into the pad.

Nothing failed. The flag was set, the text was drawn, the existing test counted well
over two hundred chip pixels and passed. **Only a rendered frame said otherwise** — the
same lesson as chapter 118, which is where the habit of actually opening the thing came
from.

The fix is a rule rather than a number: the chip anchors above the **pad** when there is
one, and above the hotbar when there is not. The toast keeps the hotbar anchor, because
it is centred and clears the pad's column on its own — two anchors, because the two
things that float above the HUD are aligned differently and only one of them meets the
pad.

And it became a test, stated as the rule and not as a pixel budget. The chip's
background is opaque, so a scan finds where the chip actually *is*, and then no control
may be laid out across it:

```cpp
const farm::Box chip_box = bbox_of(0xFF301A20u);
for (const farm::Box& b : all_controls) CHECK(!overlaps(chip_box, b));
```

A pixel count would have needed a threshold, and a threshold would have needed a guess.

## Thirteen boxes change what a geometry test is

Six rectangles can be eyeballed. Thirteen cannot, and two of them overlapping by four
pixels is a coin toss the player always loses and no screenshot ever shows. So the
geometry is checked as a **property over a sweep of screen sizes** rather than as
landmarks on one:

```cpp
for (int w = 480; w <= 1920; w += 13)
  for (int h = 260; h <= 1200; h += 17)
    for (int c = 0; c < 2; ++c) { ...every live box is on screen; no two overlap... }
```

Odd steps, so the samples do not all land on multiples of the button size and miss the
rounding. Roughly 12,000 layouts, and the sweep asserts about **itself** too —
`with_pad > 100 && without_pad > 100` — because a sweep that proves one regime twice and
the other not at all is the failure mode of every sweep ever written.

## What was checked

| Claim | How |
|---|---|
| every live box is on screen, and no two overlap | property sweep, ~12k layouts × 2 conflict states |
| `save` and `keep`/`take` never coexist | asserted in the same sweep, both directions |
| the same pixel means `save` on one layout and `keep` on the other | `read()` on both, cross-checked |
| the hotbar is 44 tall when the pad fits, 24 when it does not | sweep, tied to `visible()` |
| every hotbar slot answers a tap, and the right one | scene test, slots pressed out of order |
| the tapped tool is the tool that FIRES | Harvest tills nothing, Hoe tills — same tile |
| a tap on the hotbar does not work the tile under it | soil count unchanged across the tap |
| the save button writes THIS world | file re-read, `game farm`, `var day`, `var gold` |
| both conflict answers work by thumb | two scenes from one recipe: `take` adopts, `keep` uploads |
| the seat returns to `save` after a conflict, and saves | same scene, after resolving |
| **a dialogue can be finished by tapping** | ≤20 taps, `talking()` false, was infinite |
| a tap off the options does not answer the question | option count still 3 afterwards |
| a dialogue touches neither the soil nor the player | counted across the whole conversation |
| nothing is drawn over the config chip | bbox of its opaque background vs every control box |
| nothing on screen reacts to a pointer the dialogue owns | same frame held over `use` and idle |
| an absent control leaves no ghost in the corner | top-left 16×20 block uniform, both layouts |
| 20 mutations | 18 killed, then 2 — see below |

## The two that survived were the same mistake

Twenty mutations, eighteen dead on the first pass. The two that lived were not related
by file or by feature:

| | |
|---|---|
| `if (b.empty()) return;` deleted from the button renderer | SURVIVED |
| the pad drawn during a dialogue (`&& !talking_` deleted) | SURVIVED |

They are the same mistake. Every test in this chapter asks *can this be pressed* — the
sweep, the cross-layout pixel, the taps that change the world. **Nothing asked whether
something drawn can be pressed at all.** One draws a control that was never laid out;
the other draws six that the input path returns before reaching. Both look completely
normal, and both are the founding bug of `controls.hpp` — *drawn in one place, hit in
another* — arriving from the side nobody was watching.

They needed two claims, and both are behavioural rather than pixel budgets:

**Nothing reacts to a pointer the dialogue owns.** Hold the mouse over `use`, render;
release, render; the frames must be identical. Held rather than pressed, so the dialogue
itself ignores it, and `dt = 0` so the typewriter does not advance and turn a real
difference into an unreadable one.

**An absent control leaves no ghost.** An empty `Box` is `{0,0,0,0}` and the label is
centred *in* the box, so a renderer that draws one anyway puts its second glyph at about
`(0..14, 0..10)` in physical pixels. In a correct frame the leftmost thing on screen is
the "D" of "Day", at x = 18 — so the top-left 16×20 block is one flat colour, and it is
asserted on both layouts, since `save` and `keep`/`take` take turns being the absent one.

That guard, incidentally, is **not** the redundant kind. Four chapters running (121, 122,
123, 125) ended by deleting a guard that could not change an answer; this one changes it,
and the way to tell was to run the mutation and look, rather than to reason about it.

## Ceilings

- **One finger.** SDL synthesizes a mouse from a touch, so only one contact is seen:
  you cannot hold "walk east" and tap "use". Unchanged since chapter 124, and the reason
  the controls need no new event type at the platform seam.
- **No `F9` on screen.** Loading discards the day and there is no modal to confirm with.
- **Choice navigation by keyboard is still arrows-only.** A tap picks a row directly, so
  the cursor is not needed by thumb — but a d-pad tap does not move it either.
- **The pad is drawn on desktop too, and never auto-hides.** There is no reliable way to
  ask "is this a touch device"; the choice is between guessing and showing.
- **The controls are the farm's, not the engine's.** `iso` and `colony` have none, and
  nothing here is reusable yet — it is one game's layout, in that game's directory.
- **The hotbar's seed sub-label can overflow its slot** for a long crop name; it is 62
  pixels wide and "parsnip x0" nearly fills it.
- **Frame cost still unmeasured.** `--bench-ui` does not run the farm.
