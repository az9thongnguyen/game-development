# Chapter 124 — Playable by hand

> Code: `src/games/farm/controls.{hpp,cpp}` (new) · `src/games/farm/farm_scene.{hpp,cpp}` ·
> `src/engine/renderer2d.{hpp,cpp}` ·
> Tests: `tests/test_farm.cpp`, `tests/test_farm_scene.cpp`, `tests/test_aa.cpp`

## Tóm tắt (VI)

Farm chạy được trong trình duyệt từ chương 118, và **không chơi được trên điện thoại**
suốt từ đó: mọi động từ đều là một phím. Chương này là nửa còn lại — một d-pad và hai
nút hành động vẽ đè lên thế giới.

Nó đọc **CON TRỎ, không phải sự kiện chạm**, và đó là toàn bộ lý do nó nhỏ. SDL **tự
tổng hợp chuột từ ngón tay**, nên một cách hiện thực phục vụ cả chạm, click lẫn
trackpad — **platform seam không cần thêm loại sự kiện nào**. Cái giá: **mỗi lúc một
ngón**. Với game lưới bước một ô mỗi lần nhấn thì đổi được, và nó được **gọi tên là
trần**, không để người sau tự phát hiện.

**MỘT hàm `layout()` trả lời cho cả renderer lẫn hit test.** Nút vẽ một chỗ mà bắt một
chỗ khác là lỗi **không thấy được trong ảnh chụp**: nút trông đúng và không làm gì.

`consumed` là nửa kia. Con trỏ **nằm trên** một nút — dù có nhấn hay không — phải chặn
thế giới đọc nó, nếu không thì một cú chạm vào d-pad **cũng cày luôn ô đất bên dưới**.

**Vừa hay không phải chuyện pixel mà là chuyện TỈ LỆ.** Nút phải giữ 44 pixel logic mới
bấm được không cần nhìn, nên trên framebuffer nhỏ thì pad **thôi là lớp phủ và trở
thành cả màn hình**. Hai luật có lý do: tối đa **nửa chiều rộng**, tối đa **hai phần
năm chiều cao**. Bản đầu dùng ngưỡng pixel cố định và vẽ pad đè lên framebuffer retro
480×270, nơi ba nút chiếm nửa chiều cao.

**Một bug renderer lộ ra:** `draw_round_rect` vẽ **bốn cạnh thẳng bằng bản sao đục** và
**bốn cung góc bằng đường có alpha** — một lời gọi cho ra **hai hành vi**. Mười một
chương không ai thấy, vì mọi viền trong dự án đều đục cho tới khi một d-pad cần viền mờ.

## The gap that had been there since chapter 118

The farm has run in a browser for six chapters. It has never been playable on a phone,
because every verb in it is a key: WASD to walk, Z to use, Q to cycle seed, 1–4 for
tools, F5/F9 to save. A touch device has none of them.

That is the whole of this chapter: a d-pad bottom left, two action buttons bottom right.

## It reads the pointer, not touch events

The obvious design is a `platform::Touch` array at the seam, fed from
`SDL_FINGERDOWN`/`MOTION`/`UP`. It is also the wrong first move, because SDL already
synthesizes a mouse from a finger by default. Reading the pointer means:

- one implementation serves a tap, a click and a trackpad
- the platform seam gains **nothing** — no new event type, no new state to edge-detect,
  no second path through `backend_sdl.cpp`
- everything that already reads a mouse (the whole Studio, `ui::Context`) keeps working
  on a touch device for free, which turning synthesis off would have broken

The price is exactly one thing: **one finger at a time.** You cannot hold "walk east"
and tap "use" together. For a grid game that steps one tile per press that is a fair
trade, and it is named here as the ceiling it is rather than left to be discovered by
somebody trying to sprint and swing at once.

## One layout, two readers

```cpp
Layout layout(int w, int h);
```

The renderer calls it. The hit test calls it. Nothing else knows where a button is.

A control drawn in one place and hit in another is the specific bug this shape exists to
prevent, and it is the one bug a screenshot cannot show you: the button looks perfect and
does nothing. Every other arrangement — constants shared between two functions, a rect
stored at draw time and read at update time — has a state in which they disagree.

### `consumed`, and why it is set by position

```cpp
for (const Box* b : boxes)
    if (b->contains(p.x, p.y)) { a.consumed = true; break; }
```

Not `if (down && contains)`. A pointer merely *resting* over a control has to stop the
world reading it too, or the tile under the d-pad highlights and then reacts to a click
that was meant for the pad. Without this the same tap both walks and tills, which is what
makes an on-screen pad feel *broken* rather than absent.

### Held versus edge

Directions are **held**, like the arrow keys they stand in for: a thumb rests on `right`
and the player walks. The actions are **edges**, like Z and Q. A held `use` that repeated
would hoe sixty times a second and drain a day of energy in one press — so the test holds
it for thirty frames and counts the energy spent.

## Fitting is a question about proportion

A button has to stay 44 logical pixels to be hit without looking at it. That is a floor,
not a preference, which means on a small framebuffer the pad stops being an overlay and
becomes the screen.

```cpp
if (pad_w + action_w > w / 2) return Layout{};    // at least half the width is world
if (pad_h > h * 2 / 5) return Layout{};           // ...and enough height to see where you walk
```

The first version used fixed pixel minimums, and the test caught it immediately: it drew
the pad over the 480×270 retro framebuffer, where three 44 px buttons are half the
screen's height. Both that size and the farm's own are now pinned, so moving the threshold
has to be a decision rather than a slip.

An empty layout means the controls are absent and nothing is consumed — on a screen that
small the keyboard is the only honest answer, and **a control that covers what it acts on
is worse than one that is not there.**

## The renderer bug the d-pad found

The buttons wanted a faint outline. They came out with **solid straight edges joined by
faint curves**, which looked like a broken button and was not one:

```cpp
fill_phys(px + pr, py, pw - 2 * pr, t, c);    // top edge — an OPAQUE copy
...
if (ring > 0.0f) blend_cov(X, Y, c, cov_of(ring));   // corner arc — alpha-respecting
```

`draw_round_rect` painted its edges through a sink that ignores alpha and its arcs through
one that honours it. One call, two behaviours. `draw_rect` had the same silent problem,
and neither has a `_blend` sibling a caller could reach for instead — so honouring the
alpha *there* is the only place it can happen at all.

It survived eleven chapters because every outline in this project was opaque. The test
now pins both directions: a translucent outline blends by the same amount on a straight
edge and on an arc, and an opaque one still lands on the exact colour so the fast path
stays a fast path. It also checks `fill_round_rect`, which was already correct — so a
future "optimisation" there has to be a decision.

## Two mutation survivors, and only one was a code problem

| mutation | verdict |
|---|---|
| delete `if (!a.consumed) return a;` from `read()` | **redundant guard** — every branch below already tests `contains`. Deleted. |
| delete `&& !act.consumed` from the scene | **hole in the test.** Kept, and the test fixed. |

The second one is the interesting one. On the 640×360 test viewport the 24×18 map is
letterboxed in the middle, so no control ever covers a tile the player can stand beside —
the veto was *unreachable*, and removing it changed nothing.

Making it reachable took two corrections, both of which are the kind of mistake that
leaves a test looking like it covers something:

1. **A smaller viewport**, so the map reaches the controls. 500×380: the map fills it and
   the `seed` button sits over open field.
2. **Placing the player instead of walking them.** The map is smaller than most viewports,
   so the camera clamps against its bounds and the corners land wherever those bounds put
   them — walking cannot reach a chosen pixel, and a save file can (`px`/`py` have been in
   the save format since chapter 105).

And then a third, found by the mutation surviving *again*: the test pressed `W` to face
north **after** placing the player, and `W` also walks. The step left the button two tiles
away, the world path had nothing to do either way, and the mutation lived. Facing and
moving are the same key; the placement has to come second.

`seed` is the button the test presses, because it cycles the seed and does **not**
interact — so facing is the only thing the missing veto could change, and the assertion is
about one variable rather than about a whole world.

## What was checked

| gate | result |
|---|---|
| `ctest` | 76/76 |
| ASan + UBSan | 76/76 clean |
| mutations | 7 run, 7 killed (one after deleting a redundant guard, one after fixing the test twice) |
| golden path | inspect → verify (exit 0) → hub, green; release id unchanged |
| browser | a **real touch** on a 390×844 phone viewport with `Emulation.setTouchEmulationEnabled`: holding the d-pad's `>` walks the player east across the field, the button lights up, and the clock advances |

## Ceilings

- **One finger.** SDL's mouse synthesis reports the first touch only, so no holding a
  direction while tapping an action. Real multi-touch means finger events at the platform
  seam, and that is the next thing here if the game ever needs a diagonal or a run button.
- **Always drawn, never auto-hidden.** There is no reliable way to ask "is this a touch
  device" — SDL hands us a mouse either way, by design — so the choice was between
  guessing and showing. It is drawn faint, and a mouse player gains a second way to walk
  rather than losing anything.
- **Only the farm.** The colony and iso games, and every lab, are still keyboard-only. The
  layout lives in `farm_core` rather than in a shared UI lib, because one consumer is not
  a pattern.
- **Tools and save are not on screen.** 1–4, F5 and F9 have no buttons; the hotbar is
  drawn but not tappable. A phone player can walk, use and cycle seed, and nothing else.
- **No gesture beyond a tap and a hold.** No swipe, no pinch, no long-press.
- **Nothing measured.** The pad draws six rounded rects per frame with alpha; no
  before/after frame cost was taken.
