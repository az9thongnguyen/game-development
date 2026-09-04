# Chapter 119 — The pointer, and the first thing that needed one

> Code: `src/games/studio_shell/play_viewport.{hpp,cpp}` ·
> `src/games/farm/farm_scene.{hpp,cpp}` · `web/shell.html` ·
> Tests: `tests/test_shell_golden.cpp`, `tests/test_farm_scene.cpp`

## Tóm tắt (VI)

Chương 118 mở được cửa sổ. Chương này là **lần đầu có người bấm** — và hoá ra chuột đã
hoạt động sẵn; cái sai là **toạ độ trong bài test của tôi**, đọc ra từ một ảnh chụp mà
canvas đang bị CSS thu nhỏ 0.82 lần. Ghi lại đúng như vậy, vì "click không ăn" trông y
hệt một lỗi thật.

Đã bấm thật, trong trình duyệt: **Project** (mở asset browser), **Play → Play** (FPS
raycaster chạy trong Studio), và **vẽ một ô tile** trong Map workspace — tab đổi thành
`Map *`, status ghi `tile 7, 7`, `Undo` sáng lên, hint ghi `undo: paint`.

**Cái được xây:** chuột **vào được Play viewport**. Chương 115 cố ý không đưa con trỏ
vào game với lý do *"một viewport nói dối vị trí con trỏ còn tệ hơn một viewport thừa
nhận nó chưa có"* — đúng, **khi chưa có cách kiểm chứng phép biến đổi**. Giờ kiểm được,
nên phép tính được làm cho tử tế:

- Ánh xạ qua **cái rect mà `draw()` vừa vẽ vào**, không qua một hệ số scale lưu riêng —
  panel nhỏ hơn một khung game thì blit là *fit*, không phải bội số nguyên.
- Con trỏ **trễ một frame** (update chạy trước draw). Chấp nhận, và nói rõ: cách khác là
  tính layout hai lần rồi phải giữ hai bản khớp nhau — đúng cái lỗi dự án này gặp mãi.
- **Bấm ngoài tranh là của Studio.** Nhưng một cú bấm **bắt đầu bên trong** thì giữ con
  trỏ cho tới khi nhả, dù kéo ra ngoài — không có điều đó, mọi cú kéo kết thúc bằng một
  nút mà game tưởng vẫn đang giữ.

**Và con trỏ phải có người tiêu thụ** (đúng D15). Farm là game đầu tiên đọc vị trí
chuột: rê lên một ô **kề bên** thì nhân vật quay mặt về đó, bấm thì dùng công cụ lên ô
đó. Kiểm chứng cuối: bấm vào một ô **bên trong game đang chạy trong Play viewport, trong
trình duyệt** → hiện `tilled the soil` đúng ô.

**8 mutation, giết 7.** Cái sống sót được ghi lại thay vì giấu — xem *Ceilings*.

---

## The bug that was in the test

The first click did nothing. The second did nothing. Nothing highlighted, nothing
moved — which is exactly what a broken input path looks like.

It was not broken. The DOM was receiving the events on the canvas:

```
mousemove 271,332 target=canvas
mousedown 271,332 target=canvas
```

and SDL was receiving them correctly too — a temporary `EM_ASM` probe reported
`mx=191, my=315` against a window of `1280x720`, which is where the click actually was.

The mistake was mine. The canvas is **CSS-scaled**: 1280×720 internally, 1051.7×592.5
on screen, a factor of 0.8216. I had read the nav-rail button positions off a
*screenshot* and used them as *game* coordinates. Logical x=196 — where I thought
"Project" was — is 8 px outside the rail, which ends at 187.

Recording it because "the click did nothing" is indistinguishable from a real input
bug, and the reflex is to go looking in the platform layer. The cheap discriminator is
to ask the DOM and the app what they each saw, rather than to reason about the picture:

```js
canvas.addEventListener('mousedown', e => hits.push(e.clientX + ',' + e.clientY), true);
```

With the arithmetic done properly, the harness clicks in **game** coordinates and maps
them out to the viewport itself:

```js
const vx = rect.x + (lx / rect.iw) * rect.w;   // rect.iw = canvas.width, rect.w = CSS width
```

## What a click proves that a synthesized InputState does not

The offscreen tests have been driving this shell for six chapters. Everything they
claimed held up. What the browser added is that the claims are about the same object
the user touches: hit-testing against the real layout, at the real size, through the
real event path, with the real scaling in between.

Three clicks, three sections of the ledger closed:

- **Project** — the asset browser opened with `maps/level_00.map` selected, the
  *shippable* badge, the package hash, Copy and Re-inspect.
- **Play → Play** — the FPS raycaster running inside the Studio: `playing fps 640x400`,
  `56 steps`, *the game has the keyboard · Esc returns it to the Studio*.
- **A tile** — painted. The tab became `Map *`, the status bar `maps/level_00.map *
  unsaved   tile 7, 7`, Undo lit up, and the hint read `undo: paint`.

That last one is the whole editor in one click: hit-test, the screen→tile transform,
the command pushed onto the undo stack, the dirty-as-position rule from chapter 111,
and the tab marker from chapter 116.

## The pointer into the Play viewport

Chapter 115 deliberately did not give the embedded game a pointer:

```cpp
// Mouse coordinates are in the SHELL's logical space and mean nothing in the
// game's. Rather than hand a scene a plausible-looking wrong position, hand it
// none: a viewport that lies about where the pointer is is worse than one that
// admits it does not have one yet.
out.mouse_x = out.mouse_y = -1;
```

That was right **while the transform could not be checked**. The shell had never been
clicked, so a wrong transform would have looked exactly like a right one. That is no
longer true, so the arithmetic gets done.

### Map through the rect, not through a scale

```cpp
out.mouse_x = (in.mouse_x - shown_.x) * w_ / shown_.w;
out.mouse_y = (in.mouse_y - shown_.y) * h_ / shown_.h;
```

`draw()` letterboxes at a whole-number scale *when there is room*, and falls back to a
fitted scale when the panel is smaller than one native frame. A stored integer scale
would be wrong in the second case; the rect is right in both, and it is the same rect
the picture was blitted into, so the transform cannot disagree with what is on screen.

### One frame behind, on purpose

`shown_` is set by `draw()`, and `update()` runs first. The pointer a game sees is
therefore from the previous frame's layout.

The alternative is to compute the layout in `update()` as well — which means the same
geometry written down twice, kept in agreement by hand. This project has spent four
chapters removing exactly that (four project resolves, two entry lists, two scene
editors, one price in two files). A frame of lag on a pointer is invisible; a second
copy of the layout is a bug waiting for a resize.

### A press that starts inside keeps the pointer

```cpp
grabbed_ = any_down && (grabbed_ || inside);
```

Without it, dragging off the picture would silently stop delivering — and the game
would never hear the release, leaving it holding a button forever. With it, a click on
the Studio's own chrome still belongs to the Studio: `grabbed_` can only start inside.

## Giving the pointer a consumer

A transform nothing reads is a number, not a feature — D15, again. So the farm became
the first thing in this project to consume a mouse position:

```cpp
if (in.mouse_x >= 0) {
    const tilemap::Vec2f o = cam_.origin();
    const int tx = static_cast<int>(std::floor((in.mouse_x + o.x) / kTile));
    const int ty = static_cast<int>(std::floor((in.mouse_y + o.y) / kTile));
    const int dx = tx - world_.px, dy = ty - world_.py;
    if (std::abs(dx) + std::abs(dy) == 1) {
        face_x_ = dx; face_y_ = dy;
        if (in.mouse_pressed[Left]) interact();
    }
}
```

The rule is the one the keyboard already had — you work on an **adjacent** tile — so
the pointer chooses which of the four rather than inventing a new reach. The camera
origin comes from the last render, the same one-frame trade as above and for the same
reason.

The end-to-end check is the one that matters: **click a tile inside the farm while it
runs in the Studio's Play viewport, in a browser** → `tilled the soil`, on that tile.
Browser CSS scale → SDL → shell logical space → viewport rect → the game's 640×360 →
the farm's camera → one tile. Six transforms, one click, right answer.

## Testing notes

**8 mutations, 7 killed:**

| mutation | what it broke |
|---|---|
| shell coordinates passed through unmapped | the game sees the panel's coordinates |
| no grab | a drag off the picture loses the pointer mid-drag |
| clicks on the Studio's chrome reach the game | the Studio loses its own buttons |
| `draw()` never records where it drew | there is no transform at all |
| `stop()` leaves the old rect behind | a stopped viewport still claims a picture |
| the pointer reaches any tile, not only adjacent | reach silently becomes infinite |
| the camera origin ignored in the transform | the wrong tile, off by the scroll |

The test asserts the *edges*, not the middle: the top-left pixel of the picture is the
game's origin, the bottom-right pixel is inside and is the game's last pixel, and one
pixel past either is not the game's business. A transform that is off by one survives
a centre-only check.

The test also does its own inverse arithmetic (tile → screen) rather than calling a
helper the scene shares. A test that reuses the code under test proves only that it is
self-consistent.

## Ceilings

- **One mutation survived, and is recorded rather than hidden.** Removing the
  `mouse_x >= 0` guard in the farm does not fail the suite: with the whole farm on
  screen the camera centres it, so screen (-1,-1) maps far outside the player's four
  neighbours and the adjacency rule rejects it anyway. The guard stays — -1 means "no
  pointer" in the platform contract, and reading it as a position is precisely the
  chapter-115 bug — but the assertion beside it is a contract check, not proof that the
  guard is load-bearing.
- **Software rendering, one browser, headless.** Same harness as chapter 118.
- **No wheel, no right-click, no drag inside a game** were exercised in the browser.
  The viewport passes all three through; only left press/release has been driven.
- **Touch is untouched.** No `Input.dispatchTouchEvent`, no touch controls, nothing
  tested on a phone.
- **The farm's pointer is the only game consumer.** The FPS raycaster and the colony
  still ignore the mouse, so the viewport transform has exactly one witness.
