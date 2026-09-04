# Chapter 116 — The second occupant, and the shape it changed

> Code: `src/games/studio_shell/workspace.hpp` (new) ·
> `src/games/studio_shell/scene_workspace.{hpp,cpp}` (new) ·
> `src/games/studio_shell/workspace_host.{hpp,cpp}` (new) ·
> `src/games/studio_shell/map_workspace.{hpp,cpp}` ·
> `src/games/studio_shell/studio_shell_scene.{hpp,cpp}` · `src/main.cpp` ·
> Deleted: `src/games/sandbox/sandbox_scene.{hpp,cpp}` ·
> Data: `assets/scenes/demo.scene`, `assets/projects/creator.gameproject` ·
> Tests: `tests/test_shell_golden.cpp`

## Tóm tắt (VI)

Chương 112 **cố ý không** viết interface `Workspace`. Lý do ghi thẳng trong header:
*một lớp trừu tượng chỉ có một implementation là cái khuôn đúc quanh người ở duy nhất
của nó.* Giờ có người thứ hai, nên khuôn được vẽ từ **hai** ví dụ.

Và người thứ hai không phải viết mới — là **Sandbox được hấp thụ**. `--sandbox` từng
là một Scene riêng: palette riêng, xử lý chọn/kéo riêng, phím save riêng, vẽ đè toàn
màn hình ở toạ độ cứng, dùng bộ widget **trước chương 109**, và **không có undo** —
đúng thứ mà chương 111 dựng `CommandStack` để làm.

Nay nó là một `Workspace`, và `--sandbox` là `WorkspaceHost` mỏng bọc quanh **chính
đối tượng** mà tab Scene của Studio giữ. **Một implementation, hai cái khung.** Những
thứ trước đây chỉ Studio có — undo, autosave + recovery, command palette — giờ thuộc
về cả hai, và không còn editor thứ hai để trôi xa nhau.

**Có người thứ hai đã đổi cái khuôn thật sự, trước cả khi người đó tồn tại:**

- `status()`/`hint()` chuyển **về workspace**. Shell từng tự ghép dòng trạng thái của
  map — đường dẫn, dấu bẩn, *"tile 4, 9"*. Một scene **không có tile**, nên shell buộc
  phải thôi biết nó đang vẽ tài liệu gì.
- `inspector_width()` là một **đề nghị**, không phải hằng số. Shell vẫn giới hạn ở nửa
  vùng vẽ, để cửa sổ hẹp không làm canvas mỏng hơn panel bên cạnh.
- Recovery **có mặc định**, không phải thuần ảo. Không phải tài liệu nào cũng autosave,
  và bắt một workspace không autosave viết ba hàm rỗng để nói *"tôi không có gì để
  nói"* là cách một interface bắt đầu nói dối về ý nghĩa của chính nó.

**Ba lỗi test tìm ra trong lúc được viết** — xem phần dưới. Một trong số đó là lỗi
thật đáng kể: **mọi thao tác sửa đều đang xoá lựa chọn**.

---

## The comment that was a promise

`map_workspace.hpp`, chapter 112:

```cpp
//  ponytail: one workspace, not a Workspace interface. An abstract base with a single
//  implementation is a shape with one occupant; it arrives when the second workspace
//  does, and it will be a better shape for having seen two.
```

That is a claim about the future, and this chapter is where it gets tested. Three
things went into the interface because a *second* implementation existed — and none
of them would have been obvious from the map alone.

### status() and hint() belong to the workspace

The shell used to build the status line itself:

```cpp
std::string left = map_.loaded() ? map_.path() + (map_.dirty() ? "  *  unsaved" : "  saved")
                                 : std::string("no map");
if (map_.hover_x() >= 0)
    left += "   tile " + std::to_string(map_.hover_x()) + ", " + ...;
```

Every clause of that is map knowledge. A scene has no tiles; it has actors, bounds,
and whether it is playing. The shell cannot write a line that covers both without
either a type switch or a lowest common denominator that says nothing.

So `status()` and `hint()` moved onto the workspace, and the shell became:

```cpp
g.draw_text(area.x, sy, ws.status().c_str(), ws.dirty() ? th::warn : th::text_muted);
g.draw_text(area.x + area.w - g.text_width(ws.hint()), sy, ws.hint(), th::text_muted);
```

The shell still decides *where* and *what colour* — that is chrome, and it should be
consistent. It no longer decides *what*.

### inspector_width() is a request

260 px suits a tile palette: swatches, three tool buttons, a layer list. The scene
inspector carries **sliders**, and a slider in a narrow column has too little travel
to set a value with — it asks for 300.

But the shell overrules it: `if (insp_w > area.w / 2) insp_w = area.w / 2`. A workspace
knowing what it wants and the shell knowing what will fit are different pieces of
knowledge, and the interface is where they meet.

### Recovery is defaulted, not pure

```cpp
[[nodiscard]] virtual bool recovery_pending() const { return false; }
virtual void take_recovery() {}
virtual void dismiss_recovery() {}
```

A pure virtual here would force every future workspace — a pixel editor, a dialogue
graph — to write three empty methods to say it has nothing to recover. Interfaces that
demand ceremony from the implementations that do not need it are how "implements
Workspace" stops meaning anything.

(The scene workspace *does* autosave, so it overrides all three. It just did not have
to be a condition of existing.)

## Absorbing rather than reimplementing

The alternative to absorbing the sandbox was writing a Scene workspace from scratch and
leaving `--sandbox` alone. That is the same mistake this repo keeps finding in older
code: `--project-publish` reporting one missing asset while `--project-inspect`
reported all of them (ch. 114), the Studio's `known_entries` disagreeing with
main.cpp's (ch. 114), `launch_entry` hand-synced with `kKnownEntries` (ch. 115). Two
implementations of one idea agree on the day they are written.

So `sandbox_scene.{hpp,cpp}` is **deleted** — 315 lines — and `--sandbox` becomes:

```cpp
return run_window(cfg, std::make_unique<studioshell::WorkspaceHost>(
                           std::make_unique<studioshell::SceneWorkspace>("scenes/demo.scene")));
```

`WorkspaceHost` is a Scene that runs one Workspace full-screen: canvas, inspector,
status strip, recovery prompt, toast. It is generic — it could host the map workspace
tomorrow, which is what will retire `--maplab`.

What the sandbox gained by being absorbed, none of which was written for it:

| | before | after |
|---|---|---|
| undo | none | full history, one step per gesture |
| autosave | none | on a timer, with a recovery prompt |
| command palette | unreachable | `scene.save/undo/redo/reload/play` |
| widgets | pre-ch.109 cursor API | the S2 set |
| input handling | inside `render()` | in `update()` |
| coordinates | hardcoded screen px | fit-scaled world units |

## Undo by whole-scene snapshot

`sandbox::to_scene`/`from_scene` already round-trip the world — they were written for
save/load and for the Play/Stop snapshot. So a command is a pair of scene strings:

```cpp
void SceneWorkspace::commit(const std::string& before, std::string label, std::uint64_t merge) {
    const std::string after = sandbox::to_scene(world_);
    if (after == before) return;              // nothing changed: not history
    const int keep = sel_;
    stack_.push_apply(doc::Command{std::move(label),
        [this, after]  { install(after); },
        [this, before] { install(before); }, merge});
    sel_ = keep;
}
```

No per-edit inverse to write, and no way for an inverse to be subtly wrong. It is also
**idempotent** (D21): applying `after` when the world is already `after` changes
nothing, which is what lets a drag be *recorded after it has already happened*.

That matters because a drag writes into the component sixty times a second and must be
**one** undo step:

```cpp
if (dragging_ && !in.down(MouseButton::Left)) {
    dragging_ = false;
    commit(drag_before_, "move actor");   // ONE step, from where it ended
}
```

The test states it as behaviour rather than as a count — one undo must return the actor
*all the way* to where it started, not partway:

```cpp
CHECK(cmd::run("scene.undo").ok);
pos_of(0, ux, uy);
CHECK(ux == ox && uy == oy);
```

Committing per frame instead breaks it, and does.

**Ceiling, written down**: O(n) scene text per edit. At tens of actors that is nothing.
At thousands it is a real cost, and the fix then is per-edit commands — not a cleverer
snapshot.

## Three things the tests found

### Every edit was clearing the selection

`push_apply` **applies**. Applying re-installs the world from text. Installing rebuilds
every entity — so `install()` clears the selection, because a stale index would point
at a different actor and the inspector would then edit something the user is not
looking at.

Correct in general, and wrong for the apply that `commit` itself triggers: that one
re-installs a world *identical to the one already there*. The visible result was that
nudging a slider deselected the actor you were nudging.

```cpp
const int keep = sel_;
stack_.push_apply(...);
sel_ = keep;      // a real undo/redo still clears it; this apply is an artefact
```

I did not notice this by looking at the screen — the offscreen render after an edit
looks fine. It surfaced as `CHECK(sw.selected() == 0)` failing after a drag.

### Delete had a one-frame lag

The button flags were resolved at the top of `update()`, and the keyboard was read
lower down. So a `Delete` keypress set a flag the handler had already passed, and the
actor vanished on the *next* frame. Invisible at 60 fps, and exactly the kind of thing
that reads like a bug the first time someone traces it.

The three world-touching flags now resolve **after** the keyboard read. Buttons still
work (their flag was set during the previous draw); keys act immediately.

### The centre pixel of an actor is not the actor

`CHECK(px != th::elevated && px != th::bg)` at the centre of a drawn actor failed. The
actor is drawn, then an orientation tick is drawn from its centre outward **in the
background colour** — deliberately, so a spinner visibly turns.

A probe at the centre tests the tick. The check became a **count** of non-surface
pixels inside the actor's rect, which is the property that was meant: *the actor put
its colour on the canvas*.

This is the third time in three chapters that a single-coordinate probe measured the
wrong thing. Counting is not a workaround for a flaky test; it is the more accurate
statement of nearly every visual claim.

## Where the arithmetic lives

`MapWorkspace` exposes `tile_rect(tx, ty)` so the status bar, a test and the renderer
cannot disagree about where a tile is. `SceneWorkspace` now exposes `actor_rect(i)` for
the same reason, and the test uses it to click on actors:

```cpp
const ui::Rect a0 = sw.actor_rect(0);
platform::InputState down = press_at(a0.x + a0.w / 2, a0.y + a0.h / 2);
```

Driving the real control rather than calling a setter is the point. The tab switch in
that test is a real `press`-then-`release` on `ui::tabs`, because a tab that stopped
being wired would still pass a test that assigned the index directly — which is exactly
the failure chapter 112 found when the golden test pressed a key the shell never bound.

## What is verified

- `ctest` 71/71; ASan + UBSan clean; Emscripten build green.
- Eight mutations break the suite: committing per drag frame, keeping a stale selection
  index through a restore, recording no-op edits, Stop recorded as an edit, `commit`
  clearing the selection, recovery applied without asking, declining recovery
  destroying the autosave, and a recovery that cannot be undone.
- The Scene workspace is driven through the real UI — a real tab click, real presses on
  real actor rects, the real `scene.*` commands — for place, select, drag, delete,
  undo, Play and Stop.
- `--sandbox` is covered: a `WorkspaceHost` is constructed and rendered headless, its
  workspace is asserted to have loaded the manifest's scene, and `scene.play` /
  `scene.undo` are asserted to be registered — which is the claim that the palette
  works there too.
- The starter scene asset was generated *through* `to_scene` and asserted to round-trip
  before being written. An asset the parser cannot read is worse than no asset, because
  the editor then opens on an explanation instead of a scene.

## What is NOT verified

- **Nobody has clicked any of it.** Same as the last three chapters: offscreen renders
  through the real scene and renderer, never through SDL's `present()`.
- **No pan or zoom in the scene canvas.** The whole world is fit into the panel, which
  is right for a 640×360 scene and wrong for a large one. Placing an actor you cannot
  see is the failure a fit avoids; not being able to work closely is the cost.
- **The scene workspace has no multi-select, no copy/paste, no grid or snapping.**
- **Spawner and OnOverlap cannot be edited.** They round-trip through the file and the
  palette can attach them, but the inspector shows neither. An Emitter's interval is
  editable only by hand-editing `.scene`.
- **Textures are still probed by fixed name** (`studio_00..31`, `sheet_00..07`). That
  ceiling came with the sandbox and is unchanged.
- **`--maplab` still exists and still writes `fpsmap1`.** `WorkspaceHost` is now the
  mechanism that would retire it; retiring it is a CLI-surface decision that has not
  been taken.

## Ceilings, written down

- Snapshot undo is O(n) scene text per edit.
- The shell holds its workspaces as two concrete members plus a vector of pointers. No
  allocation and no ownership question, but the set is fixed at construction; it
  becomes `unique_ptr`s the day a workspace can be opened and closed.
- `install()` clears the selection, so undo and redo always deselect. Preserving it
  would mean matching actors across a rebuild by something stable — an id in the scene
  format, which the format does not have.
