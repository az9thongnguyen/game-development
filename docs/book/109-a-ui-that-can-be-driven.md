# Chapter 109 — A UI that can be driven

> Code: `src/platform/input.hpp` · `src/platform/platform.hpp` · `src/platform/backend_sdl.cpp` ·
> `src/engine/renderer2d.{hpp,cpp}` (clip stack, `fill_rect_blend`) ·
> `src/engine/ui/{ui.hpp,ui.cpp,ui_input.hpp,theme.hpp}` ·
> `src/games/hub/hub_panel.{hpp,cpp}` · `src/games/studio_shell/studio_shell_scene.{hpp,cpp}` ·
> Tests: `tests/test_ui.cpp`, `tests/test_aa.cpp`, `tests/test_shell_golden.cpp`

## Tóm tắt (VI)

Chương 108 làm chữ hiện đúng. Chương này làm giao diện **điều khiển được**.

Trước đó thư viện `ui` có đúng bốn widget và `ui::Input` chỉ mang
`{chuột x, y, nhấn, giữ, nhả}`. Hệ quả không phải là "thiếu widget" mà là **bốn
thứ bị chặn ngay ở seam**: không Tab được giữa các control, không bấm được
`Ctrl+K`, không cuộn được danh sách, không gõ được vào bất cứ đâu. Sửa widget
không giải quyết được gì cả — phải sửa từ tầng platform lên.

Bốn quyết định đáng nhớ nhất:

1. **Input mang *ý định*, không mang *phím*.** Widget hỏi `paste`, không hỏi
   `Ctrl+V`. Chỉ một file biết phím lệnh là `Cmd` trên macOS và `Ctrl` ở nơi khác
   — đây chính là khác biệt giữa "chạy được trên Mac" và "không". Nó cũng giữ
   `ui.hpp` sạch platform, nên mọi widget test được không cần SDL.
2. **`push_clip` **giao** chứ không **thay**.** Một danh sách cuộn nằm trong panel
   không thể thoát ra ngoài panel bằng cách xin một hình chữ nhật to hơn. Đây là
   tính chất khiến việc lồng nhau đúng.
3. **Chỉ hoãn *overlay*, không làm draw list đầy đủ.** Vấn đề thật chỉ là tooltip
   khai báo sớm bị widget khai báo sau vẽ đè. Hoãn riêng overlay là bản nhỏ nhất
   giải đúng nó.
4. **Mặc định an toàn là Cancel.** Hộp thoại huỷ diệt mở ra với Cancel đang focus.
   Cái Enter đầu tiên sau khi một hộp thoại bật lên rất thường là phản xạ còn sót
   lại từ việc người dùng đang làm dở.

Kết quả đo được: Release ss=2 vẫn **1.4–2.6 ms**/frame — bằng đúng trước khi thêm
toàn bộ tầng UI mới.

---

## 1. The gap was in the seam, not in the widget list

It is tempting to look at a four-widget UI library and conclude that the fix is
more widgets. It was not. `ui::Input` carried five fields — mouse position and
three button edges — and the platform snapshot underneath carried no wheel, no
modifiers, no committed text and no auto-repeat. Every ambition anyone might have
had for the Studio was blocked two layers below where it looked blocked:

- Tab between controls — no key state reached the UI at all.
- `Ctrl+K` — no modifier state existed anywhere in the process.
- A scrolling list — `SDL_MOUSEWHEEL` was never handled.
- Typing — `SDL_StartTextInput` was never called, so no `SDL_TEXTINPUT` event was
  ever delivered.

So this chapter runs bottom-up: platform, then renderer, then the UI layer, then
the screen that uses it. The order is not stylistic. Each layer is unbuildable
until the one below it exists.

## 2. Text is what the OS says it is

The single most important line in the platform change is this one, in `init`:

```cpp
SDL_StartTextInput();
```

Without it there are no text events, and the only way to build a text field is to
reconstruct characters from key edges: `Key::A` plus `shift` means `'A'`. That
approach works, briefly, for a US-ASCII typist on a US keyboard, and fails for
everyone else — dead keys, compose sequences, IME candidates, AZERTY, Vietnamese
telex. All of that is resolved by the operating system, and the result arrives as
`SDL_TEXTINPUT`. A text field must read *that* and nothing else.

The snapshot carries it as a fixed 32-byte UTF-8 buffer rather than a `std::string`,
so `input.hpp` stays free of the STL and the snapshot stays trivially copyable.

Two smaller decisions in the same file are worth naming:

**Modifiers are not keys.** Nothing ever wants to know that Shift was "pressed this
frame"; it wants to know whether Shift is held *while something else happens*. So
they are a `Mods` struct sampled once per frame, not four more enumerators.

**Auto-repeat is separate from `pressed`.** Held Backspace should delete more than
one character. Held Space should not publish twice. Merging them forces one of
those to be wrong, so `repeated()` is a distinct question and each caller picks.

There is also a bug that this shape invites and which the code avoids explicitly:
the per-frame transients (text, wheel deltas, repeat flags) are cleared *before*
the event queue is drained, not after. Clear them afterwards and they accumulate
forever.

## 3. Intents, not keys

The UI layer never sees any of that. `ui::Input` carries:

```cpp
struct Keys {
    bool tab, tab_back, activate, cancel;
    bool left, right, up, down, home, end;
    bool backspace, del;
    bool copy, cut, paste, select_all, shift;
};
```

`engine/ui/ui_input.hpp` is the only file that translates. It is also the only file
that contains this:

```cpp
#ifdef __APPLE__
    const bool cmd = p.mods.super;
#else
    const bool cmd = p.mods.ctrl;
#endif
```

Getting that wrong is the most common way a hand-written UI feels foreign on a Mac,
and putting it in one place means it can only be wrong once. The adapter also
suppresses text and activation while a chord is held — otherwise `Cmd+A` both
selects everything *and* fires the focused button, and any chord the OS happens to
commit as text gets typed into the field.

The deeper payoff is testability. `ui.hpp` includes nothing from `platform/`, so
`test_ui` drives the entire widget set — including copy, cut, paste and a full text
editor — with a null renderer and no SDL linked at all.

## 4. Clipping: intersect, never replace

The renderer had exactly one bound on any draw: the framebuffer edge. A panel could
not contain its own contents, which is why there were no scroll regions.

```cpp
void Renderer2D::push_clip(int x, int y, int w, int h);
void Renderer2D::pop_clip();
```

The whole design is in one word of the implementation: it **intersects** with the
clip already in force rather than replacing it. That is what makes nesting correct —
a scrolled list inside a panel cannot escape the panel by pushing a larger rect —
and it is what the tests spend most of their effort on: nesting, disjoint nesting
(which must draw exactly nothing rather than inverting into a huge rect), the four
edges, restoration after pop, and an unbalanced pop that is ignored rather than
corrupting the state.

All three physical sinks clamp to it, so solid fills, anti-aliased coverage (glyphs,
Wu lines, round rects) and additive light all obey clipping with no per-primitive
work. The stack is a fixed 16 slots with no allocation, because a `Renderer2D` is
constructed fresh every frame and must stay cheap to build.

## 5. Defer only what has to be deferred

Immediate mode paints as it goes. A tooltip declared halfway through a frame is
painted over by everything declared after it. The textbook answer is a full draw
list: widgets emit commands, the commands are sorted by layer, and the list is
replayed at the end of the frame.

That is a large change — every primitive has to become a command, and the command
set has to be kept in step with the renderer forever — and it solves a problem
narrower than itself. What actually needs to be on top is tooltips and toasts.
So only those are deferred, as *data* rather than closures:

```cpp
struct Overlay { bool is_toast; Rect anchor; std::string text; Tone tone; };
std::vector<Overlay> overlays_;
```

Ordinary widgets keep drawing immediately. If this proves insufficient — a dropdown
that must clip against a scrolled parent, say — the full draw list is still there to
be built. It has not been needed yet.

## 6. What "modal" means

A scrim is decoration unless the screen behind it stops responding. `begin_inert()`
is the mechanism: everything declared after it is drawn but cannot be hovered,
clicked or focused. `confirm()` re-enables interaction for its own two controls, so
the shape at the call site is:

```cpp
if (confirming) ui.begin_inert();
... the whole normal UI ...
if (confirming) result = ui.confirm(...);
```

Focus is trapped the same way: whenever focus is anywhere but the card's own
controls, the card claims it. That single rule gives both trapping and the initial
focus, and it is where the safest decision in this chapter lives.

**The safe default is Cancel.** A destructive dialog opens with Cancel focused, not
Delete. The first Enter after a dialog appears is very often a reflex left over from
whatever the user was doing when it appeared, and the cost of that reflex should
never be an irreversible operation. A non-destructive dialog focuses its accept
button instead, which makes "Save?" a one-keystroke question.

That behaviour did not come from a design note. It came from a failing test: the
test asserted that Enter must not confirm a destructive action, and it caught that a
previously-clicked Delete button kept focus across reopenings of the dialog.

## 7. A reason, not a timestamp

`engine::publish` and `engine::promote` have always written a reason into
`releases/audit.log`. Until now the windows passed the literal string `"shell"`.

`confirm()` takes an optional `std::string* reason`; when present the card grows a
required text field and the accept button stays disabled until it is non-empty. That
is a two-line change to the dialog and it converts the audit log from a list of
timestamps into an account of why each release moved.

Refresh and Copy skip the dialog entirely — they change nothing that a channel
points at. And only promotion to **production** is marked destructive: publishing is
additive, promoting to preview is one step from reversible, and changing what
players are running is neither.

## 8. Injecting the clipboard, and why it mattered

The obvious way to wire copy-to-clipboard is for the scene to call
`platform::clipboard_set`. Doing that immediately broke the build — not the app, the
*test*. `test_shell_golden` links the scene without the SDL backend, which is
exactly what lets it drive the whole Studio headless.

So the clipboard is injected: the scene exposes `set_clipboard(get, set)`,
`main.cpp` (the composition root, and the only place that already knows about the
platform) wires it, and a test leaves it unset — copying then does nothing, which is
the truth rather than a stub pretending otherwise.

This is worth generalising. A layer that reaches for a global service is a layer
that has quietly decided who its callers are. The link error was the design telling
on itself.

## 9. Cost

The whole point of measuring in Chapter 108 was to have a number before this chapter
existed. Same machine, same session, median of 200 timed frames after warm-up:

| build | ss | before S2 | after S2 |
|---|---|---|---|
| Release | 2 | 2.4 – 4.4 ms | 1.4 – 2.6 ms |
| Debug | 2 | 11 – 18 ms | 6.2 ms |

The layout engine, the clip stack, focus tracking, the overlay list and eight new
widgets cost nothing measurable. That is not a surprise once Chapter 108's other
finding is remembered: this workload is **fill-bound**, not logic-bound — ss=2 costs
4× ss=1, exactly the pixel ratio. Per-frame bookkeeping over a few dozen widgets
disappears next to writing 3.7 million pixels.

The practical rule that follows: if the Studio ever becomes slow, the answer will be
to draw fewer pixels (dirty rectangles, cached panels), not to make the widget code
cleverer.

## What is verified, and what is not

Verified, by running it:

- 62 tests green, including a full text editor (typing, selection, copy/cut/paste,
  UTF-8 caret movement), the clip stack, layout arithmetic at several sizes, focus
  order, the modal, and the reason requirement.
- Two **mutation checks** rather than only green ticks: replacing the UTF-8 boundary
  walk with `at - 1` fails four assertions, and ignoring Shift in a caret move fails
  the selection test. Tests that have never failed on broken code are not evidence.
- The confirmation screen was rendered offscreen at the real 1280×720×2 and
  inspected, together with its negative control: reason typed → accept button is
  accent; dialog freshly opened → accept button is disabled.
- The shell lays out correctly at four window sizes; the check that scans the whole
  right-hand column caught the button row running off the edge below ~1000px.
- The Emscripten build still links.
- Frame cost measured before and after in the same sitting (§9).

Not verified:

- **The window was never opened.** Screen capture and window scripting are both
  unavailable in this environment, so every visual claim rests on the offscreen
  render — identical scene, renderer, size and supersample, but not SDL's `present()`
  path.
- **Resizing was never performed.** `--shell` is now `SDL_WINDOW_RESIZABLE` and the
  backend rebuilds the framebuffer and texture on `SIZE_CHANGED`, but no actual
  resize has been driven. What *is* tested is the half that lives in our code: the
  scene lays out correctly at four different framebuffer sizes.
- **The clipboard has never been exercised against a real OS clipboard.** The widget
  is fully tested against an injected fake; `platform::clipboard_get/set` needs a
  live SDL video context, which CI does not have.
- **`set_cursor` has no consumer yet.** It is written and unused — the splitter that
  will want it does not exist.
- **No IME or non-Latin typing was tried.** The text field reads committed text, so
  it *should* handle them; nothing here demonstrates that it does.
