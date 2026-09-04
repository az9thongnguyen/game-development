# Chapter 115 — Play: a game inside the tool that makes it

> Code: `src/engine/fixed_step.hpp` (new) · `src/engine/app.{hpp,cpp}` ·
> `src/games/studio_shell/play_viewport.{hpp,cpp}` ·
> `src/games/studio_shell/studio_shell_scene.{hpp,cpp}` ·
> `src/main.cpp` (the one entry table) ·
> Tests: `tests/test_fixed_step.cpp`, `tests/test_shell_golden.cpp`

## Tóm tắt (VI)

Studio giờ **chạy được game** ngay trong chính nó: `--shell projects/farm.gameproject`
→ section **Play** → nút Play → Farm chạy ở đúng kích thước gốc 640×360, letterbox
giữa panel, kèm **Pause** và **Step một frame**.

Quyết định trung tâm: **scene được cấp framebuffer riêng**, đúng như cửa sổ thật cấp
cho nó. Cách kia — để scene vẽ thẳng vào framebuffer của Studio dưới một clip — sẽ
biến **mọi toạ độ thành lời nói dối**, vì scene hỏi renderer "màn hình rộng bao nhiêu"
rồi canh giữa theo đó. **Game không được phép biết nó đang bị nhúng.**

Vì sao đáng nhúng thay vì cứ mở cửa sổ game? **Pause + Step**. Xem từng bước fixed một
là thứ **không thể** làm bằng cách chạy bản thật, và đó chính là lý do tồn tại của
viewport này.

Ba điều đáng nhớ:

1. **Đồng hồ được tách ra, không nhân bản.** `App::frame` bị hàn chặt vào
   `platform::framebuffer()`/`platform::input()`, nên nó chỉ chạy scene vào cửa sổ.
   Cái phải dùng chung là **accumulator**: một bản sao thứ hai sẽ clamp lệch một chút,
   hai bên khớp nhau mọi frame bình thường và **lệch đúng lúc máy khựng** — tức đúng
   lúc bug tất định khó tái hiện nhất.
2. **Bảng entry gộp làm một.** `launch_entry` từng là chuỗi `if` + literal
   `kKnownEntries` bên cạnh, kèm comment nhờ ai sửa cái này nhớ sửa cái kia. Slice này
   cần **người đọc thứ ba** — nên thay vì thêm bản sao, gộp cả ba: launch, validate và
   play đều suy ra từ **một bảng**.
3. **Input bị chặn cửa, không phải chuyển tiếp.** Không focus → game **không nhận gì**.
   Có focus → nhận tất cả **trừ** chord của Studio và Escape. Game ăn được `Cmd+K` sẽ
   làm palette không với tới được; game nuốt Escape sẽ **nhốt** bàn phím trong viewport.

Viết test lộ ra một chỗ sai thật: "không nhận gì" từng là `InputState{}` mặc định, mà
chuột của nó nằm ở **(0,0)** — một vị trí có thật trong không gian của game. Game không
focus đang bị bảo rằng con trỏ **đậu vĩnh viễn ở góc trên trái**. Nay cả hai đường đều
báo *không có con trỏ*: toạ độ của shell vô nghĩa trong game, và **một câu trả lời hợp
lý nhưng sai còn tệ hơn một sự vắng mặt trung thực**.

---

## Correcting chapter 114

Chapter 114 closed by saying the Play viewport "needs `App` to hold a sub-scene, and
`app.hpp` has one `unique_ptr<Scene>` set in its constructor with no setter."

That was wrong, and it is worth saying why rather than quietly fixing it. Nothing stops
the Studio from owning a `unique_ptr<Scene>` itself — it already owns a map document and
a command stack. `App`'s ownership was never the obstacle.

The real obstacle is one line further in:

```cpp
void App::frame(double dt) {
    ...
    while (accumulator_ >= kFixedDt) {
        scene_->update(kFixedDt, platform::input());   // <- the window's input
        ...
    }
    gfx::Renderer2D renderer(platform::framebuffer(), platform::supersample());
    //                       ^^^^^^^^^^^^^^^^^^^^^^ the window's framebuffer
```

`App::frame` can drive a scene into the window and nowhere else. And the piece worth
sharing is not the ownership — it is the **accumulator**, which is the thing that makes
`update()` deterministic and which must not exist twice.

I had written the claim from the shape of the header without reading the body. It is a
small error with a specific lesson: a statement about what the code requires is a claim
about the code, and it should be verified the same way any other claim is.

## The clock that must not be copied

```cpp
class FixedStep {
public:
    explicit FixedStep(double dt = 1.0/60.0, double max_frame = 0.25);
    int advance(double real_dt);   // how many fixed updates to run NOW
    double dt() const, time() const, alpha() const;
    void reset();
};
```

Header-only, pure, no platform and no scene — so its test just includes it.

The reason it is worth extracting rather than re-typing ten lines is the failure mode
of the copy. A second accumulator would not look wrong in review. It would clamp at
0.2 instead of 0.25, or compare with `>` instead of `>=`, and the two would agree on
every ordinary frame. They would disagree the moment the machine hiccuped — which is
exactly when a determinism bug is least reproducible and most expensive.

`test_fixed_step` mutation-tests four of them:

| Mutation | What it breaks |
|---|---|
| clamp *after* accumulating | the spiral of death returns: a 2 s stall queues 120 updates |
| `>` instead of `>=` | one step lost per exact frame — ~10 ms of drift per second |
| `reset()` clears the total but keeps the leftover | a restarted game inherits the previous run's fraction |
| assign the accumulator instead of adding | time stops entirely when every frame is shorter than one step |

The last one is not hypothetical. It is the bug the farm's own clock had before chapter
113 found it: `(int)(dt / 0.6)` truncates to 0 at 60 fps, so a game minute never
arrived. Writing the general version means it can only be written once.

## Its own framebuffer, or every coordinate is a lie

```cpp
platform::Framebuffer fb{pixels_.data(), w_, h_, w_};
gfx::Renderer2D sub(fb, 1);
const engine::Context ctx{sub, none, real_dt, clock_.time(), clock_.alpha(), font};
scene_->render(ctx);
```

That is the whole embedding. The scene is handed a renderer over a buffer of its own,
sized exactly as `--project` would size its window, and it draws at `(0,0)` into a
`640×360` screen — because that *is* its screen.

The tempting alternative is `push_clip(area)` and let the scene draw into the Studio's
framebuffer. It is fewer lines and it is wrong, because a scene does not draw blindly:
it calls `g.width()` and `g.height()` and centres a HUD, anchors a status bar to the
bottom, and computes a camera viewport. Every one of those answers would be the
*Studio's* size. The game would be correct about nothing except the top-left corner.

Two consequences follow, and both are honest rather than clever:

- **ss = 1.** The real window may supersample; here the frame is nearest-scaled up
  afterwards, so SSAA would cost 4× the fill to be thrown away by the upscale.
- **Whole-number scaling.** A pixel game at 1.37× has rows of different heights, and
  the banding lands on exactly the art the viewport exists to show. The panel is
  1030×580 and the farm is 640×360, so the scale is 1 and there are wide margins. That
  is the correct trade: crisp and small beats large and smeared, for this kind of game.

`render()` happens inside `draw()`, once, rather than inside `update()` — which is the
same split `App` makes one level up, for the same reason. `StudioShellScene::update`
may be called several times per rendered frame when the machine stalls; rendering
230,400 pixels on each of them would waste the work that made the frame late.

## Pause, Step, and why the viewport earns its keep

An embedded player that only runs the game is a worse version of running the game. What
it can do that a window cannot:

```cpp
void step_once() { pending_step_ = true; }
```

```cpp
if (paused_ && !pending_step_) return;      // paused stops the CLOCK too
if (pending_step_) {
    pending_step_ = false;
    clock_.advance(clock_.dt());
    scene_->update(clock_.dt(), gate(in, focused));
    ++steps_;
    return;                                  // exactly one, then stop again
}
```

Pausing stops the clock, not only the scene. A paused game whose simulated time kept
climbing would jump forward on resume — which is the bug that makes people distrust a
pause button and stop using it.

`Step` advances **exactly one** fixed step regardless of how much real time passed
while the operator was looking at the frame. The `return` is the whole feature: without
it, `step_once` becomes "resume", and the two mutations that make it so are both caught.

## Gating input rather than forwarding it

```cpp
platform::InputState gate(const platform::InputState& in, bool focused) {
    if (!focused) return nothing();
    if (in.mods.super || in.mods.ctrl) return nothing();   // chords belong to the Studio
    platform::InputState out = in;
    out.key_down[esc] = out.key_pressed[esc] = ... = false;   // Escape releases focus
    out.mouse_x = out.mouse_y = -1;
    ...
}
```

Three rules, each of which exists because breaking it produces a specific trap:

- **Unfocused receives nothing.** Otherwise typing in the Studio drives the game.
- **Chords never reach the game.** A game that could see `Cmd+K` would make the command
  palette unreachable while it had focus.
- **Escape is reserved.** A game that swallowed it would trap the keyboard inside the
  viewport, with no key left that gets you out.

### What the test found

`nothing()` started out as `return platform::InputState{}`. The probe scene counted how
often it was told the mouse was somewhere, and the count was not zero.

A default-constructed `InputState` has `mouse_x = mouse_y = 0`. That is not "no mouse".
It is a real position — the game's top-left corner — and an unfocused game was being
told the pointer was parked there forever. No button was down, so nothing visibly broke;
a scene that highlights the tile under the cursor would have highlighted the corner tile
for as long as the Studio was open.

Both paths now report `-1`, and the comment says why: the shell's coordinates mean
nothing in the game's space, and handing a scene a plausible-looking wrong position is
worse than handing it none.

This is the small kind of bug that only a test written from the *scene's* point of view
finds. Watching the screen would never have shown it.

## One entry table

`main.cpp` had this shape:

```cpp
int launch_entry(const std::string& entry) {
    if (entry == "fps")  { platform::Config cfg; ...; return run_window(cfg, ...); }
    if (entry == "farm") { platform::Config cfg; ...; return run_window(cfg, ...); }
    ...
}
// The entry ids a project manifest may name — kept in sync with launch_entry.
const std::vector<std::string> kKnownEntries = {"fps", "farm"};
```

"Kept in sync with" is a comment asking a human to be a build step. Chapter 114 is
entirely about what happens to lists written down twice, and this slice needed a
**third** reader — the Play factory. So instead of adding one:

```cpp
struct Entry {
    std::string      id;
    platform::Config cfg;                                   // window + native size
    std::function<std::unique_ptr<engine::Scene>()> make;
};
const std::vector<Entry>& entries();
const Entry*              find_entry(const std::string& id);
const std::vector<std::string>& known_entries();            // DERIVED
```

A game can no longer be launchable-but-unknown (the manifest refuses an entry the build
can run) or known-but-unlaunchable (validation passes, then the launch fails). And the
Play viewport builds its scene from the same table `--project` launches from, so the
game that plays in the Studio is the game that ships.

The factory itself is **injected** into the scene, like the clipboard before it (D10):
`play_viewport.cpp` must stay linkable without SDL, because that is what lets
`test_shell_golden` drive the whole shell headless.

## What is verified

- `ctest` 71/71. `test_fixed_step` is new; four mutations break it.
- The viewport's mechanism is proven against a scene that **counts its own ticks** —
  updates, renders, and whether it saw a given key or a mouse position. Six mutations
  break it: forwarding input while unfocused, letting chords through, `Step` resuming,
  `stop()` keeping the clock, fractional scaling, and a paused viewport that keeps
  stepping.
- A **real** `farm::FarmScene` then runs 180 fixed steps inside a real
  `StudioShellScene`, driven through the scene's own `update()` — the path `--shell`
  takes — and the composed screen is asserted to leave the Studio's nav rail untouched.
  A game drawing into its own buffer cannot paint over the chrome, and that is checked
  rather than assumed.
- The frame is asserted to be a **game** and not a blank rectangle, by counting distinct
  colours inside it. A scene that failed to load its map would clear to one colour and
  pass any "did we draw something" check that only looks for non-background pixels.
- ASan + UBSan clean; Emscripten build green; golden path re-run.

## What is NOT verified

- **Still nobody has clicked Play.** Every image in this chapter is an offscreen render
  through the same scene, renderer and framebuffer size — but not through SDL's
  `present()`, and not with a human hand on the keyboard.
- **The mouse does not reach the game at all.** Translating the pointer into the
  viewport's space is arithmetic this panel already has (`shown` is returned for exactly
  that reason) and it is deliberately not done yet: a half-correct pointer is worse than
  none, and no game reached through a manifest currently needs one. `--fps` and
  `--farm` are keyboard games.
- **Only two entries exist**, and only `farm` has been played in the viewport. `fps` is
  in the table and untested there.
- **No resize handling inside the viewport.** The game's framebuffer is fixed at start;
  making the panel bigger changes only the scale.
- **The viewport keeps running while you are on another section.** That is deliberate —
  a game does not pause because you changed tabs — but it means an expensive scene costs
  frame time in the Map workspace, and nothing warns about that.

## Ceilings, written down

- `PlayViewport` holds exactly one scene. Two side-by-side (compare a change against
  the shipped release) would need the panel to own a list, and the entry table to hand
  out more than one at a time. Neither is hard; neither is asked for.
- The frame is re-rendered every draw even while paused, which is wasted fill on a
  frozen image. Caching the last frame is one flag; it is not worth it at 1.1 ms.
- Restart is `stop()` then `start()`, so it re-runs the factory. A scene with an
  expensive constructor (a large map parse) will show that as a hitch.
