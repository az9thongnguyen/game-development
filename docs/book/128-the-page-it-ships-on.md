# Chapter 128 — The page it ships on

> Code: `web/shell.html` · `src/games/farm/farm_scene.cpp` · `scripts/web_touch_check.mjs` ·
> `.github/workflows/ci.yml`

## Tóm tắt (VI)

Chương 126 làm mọi động từ của farm **với tới được bằng ngón tay**. Chương 127 mở khoá
vẽ art trong Studio. Cả hai đều đổ về **một trang web duy nhất** mà người khác vào được —
và trang đó vẫn là **debug shell**: font mono, panel log luôn hiện, canvas bị chặn ở
`78vh`, và **không một dòng nào** về chạm. Chưa ai từng đưa ngón tay vào bản web.

Chương này biến nó thành trang mà game *xuất bản trên đó*, rồi **chứng minh** bằng
`Input.dispatchTouchEvent` thật qua CDP — không phải click, không phải mousedown — và
đọc **một giá trị** ra khỏi game (`var px` trong file save game tự ghi), chứ không so
ảnh. Ba điều đáng kể hơn tính năng:

1. **Lỗi thật nằm ở chỗ không ai ngờ: canvas bị KÉO GIÃN.** Emscripten tự đặt
   `style.width/height` inline cho canvas. Sau đó `max-width` và `max-height` **cắt hai
   trục độc lập nhau** — quy tắc giữ tỉ lệ của phần tử thay thế không còn áp dụng. Trên
   máy 390px, game 1280×720 hiện ra **390×720**: mọi assertion xanh, ngón tay chạm đúng ô
   (SDL ánh xạ qua cùng cái hộp đó), và **hình cao gấp 2,5 lần**. Chỉ ảnh chụp nói ra.
2. **`touch-action: none` KHÔNG chứng minh được là cần.** Chạy lại toàn bộ bài kiểm với
   guard lật thành `auto` → **vẫn qua**. Probe riêng cho biết vì sao: CDP bỏ qua
   `touch-action` hoàn toàn — một cú kéo cuộn trang cuộn được **đúng 110px** ở cả `none`
   lẫn `auto`. Nên nó là **tuyên bố ý định**, không phải hành vi đã kiểm; cái *được* kiểm
   là trang không cuộn, tức không có gì để cướp.
3. **SDL2 bản Emscripten dựng chuột từ ngón tay — không cần hint nào.** Đây là điều
   trước nay chỉ là câu trong tài liệu. Giờ có bằng chứng: giữ nút `>` → `px` 4 → 8;
   nhắm nhầm sang `<` → `px` 4 → **1**. Ngón tay đi sang tây thật.

---

## Both halves of "it works on the web" were untested

The bundle had been built by CI since chapter 118, and the job asserted something real —
that the preload manifest does not carry this machine's saves. But "built" is where it
stopped. Chapter 123 is why that matters: the web build had **no keyboard at all** for
three chapters, and it survived every browser check because every check used the mouse.

So there were two claims outstanding, and both were sentences rather than evidence:

- *"SDL synthesizes a mouse from a finger"* — from a manual.
- *"the farm is playable by hand"* — proved in chapter 126 with a synthesized **mouse**
  in a desktop window, not with a finger in a browser.

## Aiming: ask the game, do not recompute the rule

The check has to press the d-pad's east button. Where is it?

Chapter 126's rule is that one `farm::layout()` answers the renderer and the hit test, so
a control cannot be drawn in one place and hit in another. A test harness that computed
`44 * 390 / 640` for itself would be a **third** copy of the rule, and the first to go
stale. So the game says where it drew:

```
farm: controls 640x360 up=66,156,44,44 down=66,256,44,44 left=16,206,44,44
      right=116,206,44,44 use=580,206,44,44 seed=530,206,44,44 save=580,156,44,44
      tool0=8,308,62,44
```

One line, once per process, to stderr — the same weight and the same argument as the
platform seam's existing `renderer 'opengl' (accelerated)`: *a tap that did nothing and a
tap that **missed** are different failures, and from outside the process nothing else
tells them apart.* The harness reads it out of the page's own log element and maps
logical → CSS through the canvas's bounding box.

## Observing: a value, not a picture

The other half is knowing the game reacted. A frame diff would say *something changed* —
which chapter 125 already recorded as the weaker claim it is.

The farm writes its own save, and the save is text:

```
var px 4
var py 6
```

So: tap **Save**, read `/assets/saves/farm/slot1.sav` back through the emscripten
filesystem, hold the east button for 700 ms, tap Save again, and compare. `px` went
**4 → 8**. That is the whole proof, and it is a number.

The strongest evidence came from breaking it on purpose. Aim the same held touch at
`left` instead of `right`, and the harness reports:

```
FAIL  holding the d-pad's east button did not move the player (px 4 -> 1)
```

Four to **one**. The finger did not fail to arrive; it arrived, and walked west. Nothing
about a mouse would have shown that.

## The bug only a screenshot could show

Every assertion above passed on a page whose canvas was **390 × 720 for a 1280 × 720
game** — the picture stretched to two and a half times its height, the d-pad's square
buttons drawn as tall slots. Touch still landed on the right tile, because SDL maps the
pointer through the very same box that was the wrong shape.

The cause is a CSS rule that is easy to get wrong precisely because it usually works:

> A plain `<canvas>` is a replaced element with an intrinsic ratio, so `max-width: 100%`
> shrinks it *proportionally*. **Emscripten sets `canvas.style.width` and
> `style.height` itself.** Once those inline styles exist the element has an explicit
> size, the intrinsic-ratio rules no longer apply, and `max-width` / `max-height` clamp
> the two axes independently.

The fix is to size the canvas from its own drawing buffer, and to keep doing it:

```js
function fit() {
  const box = stageEl.getBoundingClientRect();
  const w = canvasEl.width, h = canvasEl.height;   // the drawing buffer IS the ratio
  const k = Math.min(box.width / w, box.height / h);
  canvasEl.style.width  = Math.floor(w * k) + 'px';
  canvasEl.style.height = Math.floor(h * k) + 'px';
}
new ResizeObserver(fit).observe(stageEl);
new MutationObserver(fit).observe(canvasEl, { attributeFilter: ['width', 'height'] });
```

Both observers, because the two sides move independently: the stage changes with the
window, and the drawing buffer changes when SDL sets it — after this script has run.

And the assertion that would have caught it, written as a number:

```js
const shown = rect.w / rect.h, drawn = backing.w / backing.h;
if (Math.abs(shown - drawn) > 0.02) fail('the canvas is stretched');
```

This is the **third chapter running** where a rendered frame found what the assertions
did not (126: a button drawn through a warning chip; 127: a label running off a panel).
The pattern is specific enough to name: assertions check *what a thing is*, and a frame
checks *what it looks like next to everything else*.

## A guard that could not be tested, said so

`touch-action: none` on the canvas is the line every guide gives for a browser game: it
stops the browser stealing a drag for a scroll or a pinch. Keeping it is easy. Knowing
whether it does anything is not.

Running the entire check with it flipped to `auto` **still passed**. Two readings: the
guard is useless, or the harness cannot exercise it. A separate probe settled it — make
the page scrollable, drag a finger up the canvas, and read `window.scrollY`:

| `touch-action` | page scrollable | `scrollY` after the drag |
|---|---|---|
| `none` | yes | 110 |
| `auto` | yes | 110 |

Identical. **CDP's synthetic touch does not go through the browser's touch-action
arbitration at all.** So the automated check can say nothing about this line in either
direction, and the honest thing is to write that down rather than let a passing suite
imply a guarantee it never made. The line stays — for pinch-zoom, double-tap-zoom, and
the day this layout starts to scroll — and the check that *is* behavioural sits next to
it: the page does not scroll, so today there is no scroll to steal.

## What the page had to stop being

| Was | Is | Why |
|---|---|---|
| `78vh` cap on the canvas | fits the stage, ratio kept | 22% of a phone screen spent on nothing |
| `100vh` | `100dvh` | on a phone `100vh` is the height with the URL bar *hidden*, so the page was taller than the screen |
| runtime log always visible | behind a `log` button | it is a debug console, and it was the loudest thing on the page |
| no fullscreen | a button, on the **stage** | fullscreening the canvas makes the browser stretch it to the screen's shape |
| mono debug font, `h1` heading | one 28px bar: name · status · buttons | chrome should be chrome |
| no safe-area handling | `viewport-fit=cover` + `env(safe-area-inset-*)` | the d-pad sat under the home bar |
| — | "rotate for a bigger picture" | a 16:9 game in portrait is a stamp; stretching it was the old answer |

## What was checked

| Claim | How |
|---|---|
| a real touch reaches the game | `Input.dispatchTouchEvent` with touch emulation on; the tap on Save wrote a save |
| it lands where the game says the button is | the aim comes from the game's own `farm: controls` line; aiming one button over walks the player the other way |
| holding walks the player | `px` 4 → 8, read from the save file, not from pixels |
| the picture keeps its shape | `shown/drawn` ratio within 0.02 |
| the game fills the screen | the canvas touches an edge of the stage; the stage is within 60 px of the viewport |
| the page does not scroll | `scrollHeight <= innerHeight` |
| the log is not in the way | computed `display` is `none` |
| both orientations | run at 390×844 and at 844×390 |
| the checks can fail | 4 mutations, 4 killed (no-op `fit`, log shown, tall bar, aim at the wrong button) |

## Ceilings

- **`touch-action: none` is unverified**, and now known to be unverifiable by this
  harness. A real device would settle it.
- **One finger.** Unchanged from chapter 126 — SDL synthesizes *a* mouse, so holding a
  direction while tapping an action is still impossible. This chapter proves the one
  finger arrives; it does not add a second.
- **Portrait is a stamp.** A 16:9 game on a 9:19.5 phone is 390 × 219, which makes the
  44-logical-pixel d-pad buttons 27 CSS px — under every touch-target guideline. The page
  says "rotate"; it does not fix it. A portrait-aware layout is the farm's job, not the
  page's.
- **Chrome only.** The harness drives Chrome over CDP. Safari on iOS is the browser most
  likely to differ (`100dvh`, `env()`, fullscreen on iPhone) and is not covered.
- **The Studio on a phone is untested.** `?shell=` still opens a 1280×720 workspace on a
  390 px screen. This chapter is about the games.
- **No Collection page yet** — the link opens one game, chosen in the query string.
