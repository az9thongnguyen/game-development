# Chapter 114 — One resolve, four copies, and the drift nobody chose

> Code: `src/engine/project/inspect.{hpp,cpp}` (new `inspect_core`) ·
> `src/engine/release/ops.cpp` · `src/engine/hub/hub_build.cpp` ·
> `src/engine/commands/release_commands.cpp` · `src/main.cpp` ·
> `src/games/studio_shell/project_panel.{hpp,cpp}` ·
> `src/games/studio_shell/studio_shell_scene.{hpp,cpp}` ·
> `src/games/hub/hub_panel.{hpp,cpp}`, `src/games/hub/hub_scene.{hpp,cpp}` ·
> Tests: `tests/test_inspect.cpp`, `tests/test_commands.cpp`, `tests/test_shell_golden.cpp`

## Tóm tắt (VI)

Chương này bắt đầu từ một dòng comment mà chính repo đã tự viết ra ở
`ops.cpp:37`: *"CLI và hub giữ bản sao nhỏ của riêng chúng; hợp nhất cả ba khi
xuất hiện cái thứ tư."* Asset browser của Studio **chính là cái thứ tư** — nên
đây là lúc thực hiện lời hứa đó.

Và hoá ra bốn bản sao **đã trôi xa nhau từ trước**, đó mới là phát hiện thật:

- `ops.cpp` **dừng ở asset thiếu ĐẦU TIÊN** → `--project-publish` với ba đường
  dẫn hỏng báo **một lỗi mỗi lần chạy**: sửa, chạy lại, biết lỗi tiếp theo.
- `hub_build.cpp` và `inspect` liệt kê **tất cả**.

Không ai quyết định điều đó. Đó đơn giản là chuyện xảy ra với bốn bản sao của
một ý tưởng. `engine::inspect()` giờ là bản duy nhất, và nó trả về **dữ liệu**,
không phải dòng chữ đã in.

Ba điều đáng nhớ:

1. **Studio có section `Project`** — duyệt asset (type, path, content hash, size)
   + verdict validate, vẽ từ **đúng câu trả lời** mà `--project-inspect` in ra.
2. **Nó lộ ra một bất đồng thật.** Scene tự giữ `known_entries = {"fps"}` trong
   khi `main.cpp` biết `{"fps","farm"}` → `--shell projects/farm.gameproject`
   báo *"unknown entry scene: farm"* trên đúng cái project mà
   `--project-inspect` gọi là OK. **Hai câu trả lời, một sự thật**, và trên màn
   hình không có gì nói cái nào đang nói dối.
3. **Audit log lần đầu hiện trong cửa sổ.** `engine::log()` trả dữ liệu từ khi
   release store ra đời và **chưa cửa sổ nào từng vẽ nó**. `status()` nói release
   đang *ở đâu*; chỉ log mới nói nó *đến đó bằng cách nào* và người vận hành đã
   gõ **lý do** gì.

Và ngay khi vẽ ra, log cho thấy một dòng `publish` **không có lý do** — một dòng
được ghi trước khi quy tắc bắt buộc lý do tồn tại. Panel làm nó **hiện ra**, thay
vì để nó nằm đó trông giống mọi dòng khác.

---

## The comment that scheduled its own repair

`ops.cpp` carried this, and had for several chapters:

```cpp
// ponytail: a self-contained resolve local to the ops lib — the CLI (resolve_project) and
// hub (build_hub_view) keep their own small copies; unify the three if a fourth appears.
```

That is a good comment. It names the shortcut, names the condition under which the
shortcut stops being one, and does not pretend the duplication is a design. Most
`ponytail:` comments in this repo are exactly that shape: a ceiling and its upgrade
path, written down at the moment the shortcut was taken, while the reasoning is still
in someone's head.

The Studio's asset browser needs "read the manifest, validate it, hash every declared
asset" in a *window*. That is the fourth caller. So the condition fired.

What made it worth doing immediately rather than "when it hurts" is that it had
already hurt, silently. Counting the copies:

| Copy | Where | On a missing asset |
|---|---|---|
| launch | `main.cpp::resolve_project` | lists **all** |
| inspect | `main.cpp::launch_project(inspect_only)` | lists **all** |
| publish | `ops.cpp::resolve` | returns at the **first** |
| hub | `hub_build.cpp::build_hub_view` | lists **all** |

Three agreed and one did not. Nobody decided that publish should be the terse one;
it is just what happens when the same idea is typed out four times by four different
tasks on four different days. The observable consequence:

```
$ ./build/demo --project-publish projects/broken.gameproject development "why"
not shippable: missing asset: gone1.txt
```

Fix `gone1.txt`, run again, learn about `gone2.txt`. Three broken paths, three runs.
Meanwhile `--project-inspect` on the same manifest listed all three at once. Two
verbs over one manifest, disagreeing about how much they were willing to tell you.

After:

```
$ ./build/demo --project-publish projects/_drift_probe.gameproject development "probe"
not shippable: missing asset: gone1.txt; missing asset: gone2.txt; missing asset: gone3.txt
```

## Data, not printed lines

The rule this repo keeps re-learning: **an operation returns a value; presentation is
the caller's job.** `hub_lines` learned it, then `status()`/`log()` learned it when
they moved out of `main.cpp`. `inspect()` is the same lesson applied to the one read
that every other verb starts with.

```cpp
struct InspectedAsset {
    std::string type, path;
    std::uint64_t hash  = 0;       // 0 when missing
    std::size_t   bytes = 0;       // 0 when missing
    bool          present = false;
};

struct Inspection {
    std::string path;
    bool        readable = false;   // the manifest file could be read
    bool        parsed   = false;   // ...and it was a gameproject1 manifest
    Project     project{};
    std::vector<InspectedAsset> assets;     // every DECLARED asset, present or not
    std::vector<std::string>    problems;   // validation errors, then missing content
    std::string package;                    // "" unless shippable

    bool shippable() const { return parsed && problems.empty(); }
    std::vector<PackagedResource> resources() const;
};
```

Four decisions inside that struct are worth defending, because each one is a place
where the obvious shortcut is subtly wrong.

**`readable` and `parsed` are separate from `problems`.** A manifest that is not there
is not "a project with one problem" — there is no project. A caller needs to tell
"broken project" from "no project", because the first is something to fix and the
second is probably a typo in a path.

**A missing asset stays in `assets`, in manifest order.** The tempting version filters
it out and puts the paths in `problems` instead. But an asset browser that silently
drops what it cannot find is the one browser you cannot use to find out what is wrong.
And *where* the hole is says as much as that there is one: the second of five assets
missing reads very differently from the last.

**A missing asset has no hash and no size — not zero.** `0 bytes` in a size column
reads like an empty file. The panel therefore shows `not found on disk` and no numbers
at all. A confident wrong answer is worse than an absent one, and this is a panel
whose entire job is to be believed.

**`package` is empty unless the project is shippable.** This is the one that matters
most, because the release id is derived from it. `resources()` deliberately excludes
the missing assets — so hashing them anyway would produce a *different, shorter*
package for a broken project, and a release id that is computable from incomplete
content is a release id that can be published. An incomplete project has no package
hash. Not a provisional one.

`test_inspect` mutation-tests all four: stopping at the first missing asset, dropping
missing assets from the list, hashing an incomplete project, and reordering the problem
list are each caught.

## Ordering the problems by how much they explain

```cpp
for (const auto& e : validate(in.project, known_entries)) in.problems.push_back(e);
// ...then, per asset:
in.problems.push_back("missing asset: " + a.path);
```

Validation errors first, missing content second. A wrong entry id explains the whole
project; a missing sprite explains one asset. Ordering the list by explanatory power
is what makes "read the first line" usually enough — which is the only reason anyone
reads a problem list at all.

## The Studio grows a Project section

`projectui::draw_project_panel` is a pure function of `(Inspection, layout)` returning
what was clicked — the same shape as `hub_panel`, for the same reason: it performs
nothing, so both the data and the rendering are testable without a window, and the
scene keeps ownership of everything that survives a frame (including the selected row).

The layout has one decision in it that is not cosmetic:

> **The problem strip sits ABOVE the asset browser.**

Under a scrolling list, a warning becomes something you have to go and look for. The
verdict is why you opened the panel; putting it where the eye already is costs nothing
and putting it below the fold costs everything.

The rest follows from the struct: a `shippable` / `N problems` badge as a **word and a
colour** (never colour alone), the package hash with a Copy button that is disabled
when there is nothing to copy, and a detail card for the selected asset.

## The divergence the panel found on its way in

The scene was constructed like this:

```cpp
StudioShellScene::StudioShellScene(std::string project_path)
    : project_path_(std::move(project_path)),
      known_entries_{"fps"},          // <- its own copy of the list
```

while `main.cpp` had:

```cpp
const std::vector<std::string> kKnownEntries = {"fps", "farm"};
```

`validate()` fails a manifest whose `entry` is not in the list. So
`--shell projects/farm.gameproject` opened onto a Hub that said **NOT shippable —
unknown entry scene: farm**, for a project that `--project-inspect` in the next
terminal called **OK**. Two surfaces over one manifest, giving opposite verdicts,
with nothing on screen to indicate which one was lying.

This is the same failure as the four resolves, one level up: a list that means
"what this build can launch" was written down twice, and only one copy was maintained
when the farm game arrived in chapter 113. The fix is the same shape too — the list is
**injected**, owned by the one thing that can actually launch an entry:

```cpp
StudioShellScene(std::string project_path, std::vector<std::string> known_entries);
```

And the test states the property rather than the symptom:

```cpp
studioshell::StudioShellScene farm("projects/farm.gameproject", kKnownEntries);
CHECK(farm.inspection().shippable());
// ...and the negative control: the list is what makes the difference.
CHECK(!engine::inspect("projects/farm.gameproject", {"fps"}).shippable());
```

Without that second line the first would pass just as happily if `known_entries` were
ignored entirely — which is precisely the bug one refactor away.

`map_asset_of` was the *fifth* copy of "read and parse the manifest". It reads through
`inspect()` now, so the workspace opens the map the browser lists.

## The audit log, finally on screen

`engine::log()` has returned the append-only history as a `vector<AuditRecord>` since
the release store was built. Nothing has ever drawn it. `--release-log` prints it in a
terminal; the two hub surfaces showed only `status()` — *where* each channel points.

The gap that leaves is exactly the one the brief's ledger still calls unproven:
`status()` tells you production is at `c95febd8`, and nothing tells you who moved it
there, when, or what they typed as the reason. The audit log has always held that. It
just had no window.

Both surfaces get it in one change, because there is one panel. `draw_hub_panel` grew
a `const std::vector<AuditRecord>&` parameter — read by the caller, because reading is
I/O and that file performs none.

**Newest first, while the file stays append-only oldest-first.** The question a release
history answers is "what just happened"; making that the last row you scroll to is how
a log stops being read. The reversal happens in the *reading*, never in the store.

That direction is pinned by a test that can actually fail — which took a moment's
thought, because "is this list the right way round" resists a probe at a fixed
coordinate:

```cpp
// Both {older,newer} and {newer} alone open with the SAME top row, so they can only
// differ one row further down. {older} alone differs immediately.
const int d_vs_new = first_diff_row(both, just_new, 340);
const int d_vs_old = first_diff_row(both, just_old, 340);
CHECK(d_vs_new > d_vs_old);
CHECK(d_vs_old < PH);          // they DO differ somewhere, or this proves nothing
```

Reverse the loop and `d_vs_new > d_vs_old` inverts. Delete the block and the second
line fires. Both were confirmed by breaking the code on purpose.

Timestamps are **UTC**. An audit log is read by whoever is on call, and two people
comparing entries should not first have to establish where each was sitting.

## What drawing it immediately showed

The very first render of the history had a row like this:

```
2026-09-04 04:40   publish   development   c95febd8…
```

No reason. It is a real entry, written before the rule that mutating commands refuse
blank arguments (D17) existed. The reason column is empty because there was nothing to
put in it.

That is the argument for D17 made visible. In a terminal, `--release-log` prints that
line and it scrolls past looking like every other line. In a column layout with the
reason as the widest field, the hole is the first thing you see — and the log stops
being a list of timestamps and becomes a record you can interrogate.

## What is verified

- `ctest` 70/70, including a new `test_inspect` whose four central claims are
  mutation-tested (first-problem-only, dropped missing assets, hashing an incomplete
  project, reordered problem list — each breaks the suite).
- The drift is verified at the CLI, not just in a unit test: a probe manifest with
  three broken paths now produces the same three lines from `--project-publish` and
  `--project-inspect`.
- `--project-inspect` output is byte-identical to before the refactor for both
  `projects/creator.gameproject` and `projects/farm.gameproject`, and the flag is now
  an alias onto `cmd::run("project.inspect", …)` rather than a second formatter.
- `test_shell_golden` renders six sections and asserts consecutive content
  fingerprints differ — the check that caught a previous version of that loop pressing
  a key the shell never bound.
- The Project panel's healthy / holed / unreadable states are asserted by **counting**
  verdict-coloured pixels, not by probing a coordinate that moves when the layout does.
- History ordering and presence are mutation-tested (reverse the loop; delete the
  block).
- ASan + UBSan clean; Emscripten build green; frame cost unchanged.

## What is NOT verified

- **Nobody has clicked any of this.** Screen capture and Accessibility are blocked in
  this environment, so every visual claim rests on offscreen renders through the same
  scene, renderer and framebuffer size — but not through SDL's `present()`.
- **The asset browser has never held a long list.** Five assets fit without scrolling.
  The scroll region is exercised by `ui::begin_scroll`'s own tests, not by this panel
  at fifty rows, and the row height / eliding was chosen by eye at five.
- **`Re-inspect` re-reads on demand; nothing watches the filesystem.** Edit an asset in
  another tool and the panel is stale until you press R. That is honest but manual.
- **The history is not filtered.** `engine::log()` takes a channel filter and the panel
  ignores it; every entry for every channel is in one list. At a few dozen rows that is
  fine and at a few hundred it will not be.
- **`Play viewport` is not here.** *(Corrected in chapter 115: this paragraph
  originally said the Play viewport "needs `App` to hold a sub-scene". That framing is
  wrong. Nothing stops the Studio owning a `unique_ptr<Scene>` itself. The real
  obstacle is that `App::frame` is welded to `platform::framebuffer()` and
  `platform::input()`, so it can drive a scene into the window and nowhere else — and
  the fixed-timestep accumulator that makes updates deterministic lives inside it. What
  chapter 115 actually had to do was extract the accumulator, not change what `App`
  owns.)* Either way it is an architectural change rather than a panel, and it deserves
  its own slice.

## Ceilings, written down

- `Inspection` re-hashes every asset on every call. Five small files is nothing; a
  project with a hundred textures will make `Re-inspect` visibly pause, and the fix is
  a mtime/size check before re-reading, not a cache invalidation scheme.
- `draw_project_panel` elides long paths from the front by trimming one character at a
  time and re-measuring. O(n) `text_width` calls per elided path — invisible at these
  lengths, wrong if a path is ever pathological.
- The history list has no filter and no paging. `engine::log(channel)` already supports
  the first; the panel does not ask for it yet.
