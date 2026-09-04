# Chapter 112 — The Studio grows a workspace

> Code: `src/games/studio_shell/{map_workspace,palette}.{hpp,cpp}` ·
> `src/engine/tilemap/map_edit.{hpp,cpp}` · `src/games/studio_shell/studio_shell_scene.{hpp,cpp}` ·
> `src/engine/commands/registry.{hpp,cpp}` (`filter`, `unregister`) ·
> `src/engine/ui/ui.{hpp,cpp}` (`hit`, `id_for`, `set_inert`) ·
> Tests: `tests/test_map_edit.cpp`, `tests/test_map_workspace.cpp`, `tests/test_shell_golden.cpp`

## Tóm tắt (VI)

Chương 110 cho dự án **một định dạng bản đồ**. Chương 111 cho nó **một ngăn xếp
undo** và **một registry lệnh**. Cả ba đều đã có test — và **không ai dùng**. Map Lab
vẫn ghi `fpsmap1`, chưa workspace nào push vào `CommandStack`, registry chưa có
palette. Đó đúng là "motion without connection" mà `docs/strategy/02 §10b` cảnh báo.

Chương này là **cái tiêu thụ chúng**: Studio mở thẳng vào workspace **Map**, sửa một
file `map2` thật, mọi thao tác sửa đi qua `CommandStack`, autosave chạy theo đồng hồ,
và `Cmd+K` liệt kê đúng những gì `cmd::all()` có.

Ba điều đáng nhớ:

1. **Một nét vẽ là MỘT bước undo — nhưng không phải bằng `merge_key`.** Stack gộp
   bằng cách giữ *revert đầu tiên* + *apply cuối cùng*; đúng cho cú kéo mà trạng thái
   cuối bao trùm mọi trạng thái trước (kéo một object), **sai** cho cú kéo *tích luỹ*
   (vẽ một nét) — revert đầu tiên chỉ khôi phục đúng ô đầu tiên. Nên `Stroke` tích luỹ
   ở chỗ khác, rồi đến stack **một lần**, mang theo giá trị cũ của **từng ô**.
2. **Xây consumer mới lộ ra hai bug thật.** `map2` không round-trip được layer tiles
   *không có tileset* — đúng thứ mà một editor sinh ra trước khi có art. Và
   `test_shell_golden` bấm `Tab` để chuyển section, phím mà shell **chưa bao giờ**
   gán cho việc đó: nó render section 0 năm lần và vẫn xanh.
3. **Từ chối recovery phải AN TOÀN.** Cancel giữ lại file đã lưu **và để nguyên
   autosave** — một cú bấm theo phản xạ không được là thứ huỷ mất công việc.

Và điểm quan trọng nhất về sản phẩm: `test_map_workspace` kết thúc bằng việc đọc file
mà editor vừa ghi **bằng chính loader của raycaster**. Đây là file `map2` đầu tiên
trong repo do *người tạo ra*, không phải do migration hay unit test sinh ra.

---

## Where this sits

Four slices in a row were plumbing. Each one was tested, and each one ended with the
same uncomfortable sentence in its verification section: *nothing consumes this yet*.

That is a real risk, not a bookkeeping detail. A core with no consumer is a core whose
interface has never been argued with. It looks finished because the tests pass, and the
tests pass because they were written by the same person who wrote the interface, against
the same assumptions. The first real consumer is where those assumptions get tested by
something that did not agree to them in advance.

This chapter is that consumer. It is deliberately narrow — one workspace, one file
format, one canvas — and it is enough to make three previously-inert subsystems do
work: `tilemap` (a map an author edits), `document` (edits that can be taken back),
`commands` (a palette that lists them).

## The shape

```
StudioShellScene            the frame: nav rail, sections, one modal at a time
  ├── MapWorkspace          the map editor: document + tools + inspector
  │     ├── tilemap::Map    the document
  │     ├── doc::CommandStack  its history
  │     └── mapedit::       edits, as doc::Commands
  ├── CommandPalette        Cmd+K over cmd::all()
  └── hubui::draw_hub_panel the Hub tab (unchanged)
```

`MapWorkspace` touches no SDL. It reads a `platform::InputState` — a plain struct of
booleans and ints — and draws through `Renderer2D`, which writes into any framebuffer
the caller owns. That is what lets `tests/test_map_workspace.cpp` synthesize a drag,
assert the document changed, undo it, save it, and reopen it with an autosave beside
it, all with no window. It is the same property that made `test_shell_golden` possible
in chapter 108, and it is worth stating as a rule: **a workspace that can only be
driven by a window is a workspace that can only be tested by a person.**

## Why a stroke is not a merge

`doc::CommandStack` already had gesture merging: consecutive commands sharing a
non-zero `merge_key` collapse into one undo step by keeping the **first revert** and
the **latest apply**. That was written for a slider drag, and for a slider drag it is
exactly right — the latest value subsumes every earlier one, and the first revert is
the value before the gesture began.

A paint stroke is not that shape. Each cell has its own previous value:

```
cmd1:  set (3,1) from 4 to 9      revert: (3,1) := 4
cmd2:  set (4,1) from 7 to 9      revert: (4,1) := 7
merged (first revert + latest apply):
       apply:  (4,1) := 9
       revert: (3,1) := 4          <-- (4,1) is never restored
```

Undo would leave the map with half the stroke still painted. The merge rule is not
broken; the stroke is simply not the kind of gesture it describes.

So `mapedit::Stroke` accumulates the gesture *before* the stack sees it, recording each
cell's own `before`, and reaches `push_apply` once. `touch()` writes through as the
mouse moves — you have to see paint appear under the cursor — which means the command
it finishes with is applied to a map that is already in the after-state. That is fine
precisely because the command writes **absolute values, not a delta**: applying it to an
already-applied map is a no-op, and applying it after an undo is the redo. Idempotence
here is not an optimisation, it is what makes `push_apply` (which applies) safe to call
on a gesture that has already happened.

The test states the property rather than the mechanism: eight cells with eight
different starting values, dragged over with one brush, then one `undo()` — and every
distinct original value is back.

## What building the consumer found

Two bugs that four green test suites could not have shown, because both live at a seam
that only a consumer crosses.

### map2 could not round-trip a map an editor makes

`to_text` wrote a tiles layer as `layer <name> tiles <tileset>`, and `load` required
that tileset field. A `tilemap::Layer` constructed in memory has an empty tileset —
which is exactly what an editor produces before any art exists — so it serialized to
`layer ground tiles` with the field simply absent, and this very parser then read the
next token, `row`, as the tileset name and rejected the file at the first data line.

The existing round-trip test could not catch it: it parsed a hand-written fixture that
*did* name a tileset, so `to_text(load(text)) == text` held. The invariant it checked
was real; the input never exercised the hole.

An empty tileset is now written as `-` and read back as empty, and there is a test that
builds a bare map in memory and round-trips it — the direction an editor actually goes.

The general lesson is worth keeping: **a round-trip test seeded only from files is a
test of the parser, not of the format.** Seed it from the struct too, because that is
where a tool starts.

### The golden test was checking one screen five times

`test_shell_golden` stepped through its four sections by feeding a `Tab` key edge.
The shell has never bound Tab to navigation — Tab belongs to the UI's focus ring, and
has since chapter 109. So the loop rendered section 0 four times, wrote four PPM files
named `hub`, `guide`, `learn` and `about` that were byte-identical, and passed every
assertion in it. Each assertion was about the nav rail, which does not change between
sections.

The fix is not just the right key. It is a check that could **fail** if this happened
again: a hash of the content area, and an assertion that consecutive sections differ.

> This is the second time in this project that a test has been found asserting things
> that were true regardless of the behaviour it claimed to cover (the first was the CI
> create-verb step in chapter 111's follow-up). Both had the same shape: the setup was
> wrong, and every assertion happened to be insensitive to the setup. A test's *setup*
> deserves an assertion of its own.

## Keys, and why the arrow keys moved

The nav rail used Up/Down. A canvas wants Up/Down. The existing code comment in the
shell said it plainly — *a key that means two things means neither* — so rather than
adding a mode where arrows sometimes navigate, the rail moved to `Cmd/Ctrl+1..5`,
which is what every tabbed application already uses, and the arrows became free.

The Hub tab keeps its unmodified `Space` / `1` / `2` / `R`, and those now check that
the command modifier is **not** held, so `Cmd+1` (go to Map) and `1` (promote to
preview) cannot be confused.

## The palette, and what it deliberately will not do

`Cmd+K` lists `cmd::all()`, filtered by a case-insensitive **subsequence** match over
`"<id>  <title>"`, so `rp` finds `release.promote`. Results stay in registration order
rather than being ranked by match quality: a ranked list means the same keystrokes
select different commands as the registry grows, and a palette whose first entry moves
is a palette you have to read instead of one you can type through.

It does **not** collect arguments. A command with an `args_help` string is listed — so
you can see it exists and what it wants — and selecting it reports what it needs
instead of running it wrong. Three validated values belong in the Hub tab's confirm
dialog, which already exists and already requires a reason; a one-line box would be a
worse version of a thing this repo has.

The workspace registers `map.save` / `map.undo` / `map.redo` / `map.reload` **against
itself**, and unregisters them in its destructor. A handler that captured `this` and
outlives the object is a dangling call the palette would happily make, and the crash
would only appear when someone closed a workspace and then opened the palette — which
is to say, rarely, and never on the machine where it was written. `cmd::unregister`
exists for that reason, and a test constructs a workspace in a scope and asserts the
registry is empty after it.

That the registry is process-global is not a wart: `--cmd` in a terminal lists the
release operations and not `map.save`, because there is no map open in a terminal. The
list is "what this process can do", and it is the same list in both places.

## Autosave, and declining safely

Autosave runs on a **timer**, not on every edit. A file write per painted tile would
make a drag stutter for no benefit; ten seconds is the most that can be lost.

The recovery prompt keeps chapter 111's rule — offer, do not apply — and adds one:
**declining must be safe.** The modal's two answers are `Recover` and `Cancel`, and
Cancel keeps the saved file *and leaves the autosave on disk*. A reflex click on Cancel
therefore costs nothing; the offer simply returns next time, and a real save clears it.
The alternative (Cancel deletes the autosave) puts the destructive action behind the
button people press without reading, which is the same mistake as a destructive dialog
that opens with the accept button focused — the thing chapter 109 fixed.

Recovery itself goes on the undo stack as a real command, with the saved version as its
revert. So the document opens dirty (the file on disk *is* older), and one Ctrl+Z takes
the user back to exactly what they had saved. It is an edit, and it behaves like one.

## What is verified, and what is not

**Ran, and the result was checked:**

- `ctest` — **68/68 green**, including the three new suites (`map_edit`,
  `map_workspace`, and the extended `shell_golden`).
- **ASan + UBSan clean** across the whole suite. The sanitizer earned its place here:
  the first run of `test_map_workspace` crashed on a heap overflow that turned out to be
  the test's own `std::vector<uint32_t> buf{w * h, 0}` — braces, so a two-element
  initializer list rather than a sized buffer.
- **Mutation-tested**, not merely green: making revert write the after-state, disabling
  the no-op filter, and removing the flood fill's value check each turned assertions
  red; restoring them turned them green.
- The Map screen and the palette were **rendered offscreen at 1280×720×ss2 and looked
  at** (`shell_map.ppm`, `shell_palette.ppm`), which is how the centring, the swatch
  row and the palette's focus ring were checked.
- **Frame cost, same machine and sitting, Release:** ss=2 `1.2–1.4 ms` — unchanged from
  after chapter 109 (`1.4–2.6 ms`). The workload is fill-bound, and the Map canvas
  draws fewer glyphs than the Hub panel it replaced as the opening screen.
- `--cmd` with no id still lists exactly the release commands, so the CLI's behaviour
  did not change when the Studio started registering its own.
- Emscripten build green.

**Not verified — stated plainly:**

- **Still no real window.** This environment grants neither screen capture nor
  accessibility control, so every visual claim rests on offscreen renders: the same
  scene, renderer, size and supersample factor, but **not** through SDL's `present()`.
  Mouse behaviour is synthesized `InputState`, not a real pointer — so drag *feel*
  (acceleration, the exact frame a stroke starts) is unproven.
- **Map Lab was not absorbed.** `--maplab` still exists and still writes `fpsmap1`.
  The Map workspace supersedes it, but deleting a mode is one of the decisions this
  project holds for its author, and it belongs with the other CLI-flag retirements.
- **The Sandbox was not absorbed either**, and so there is still only **one**
  workspace. There is deliberately no `Workspace` interface: an abstract base with a
  single implementation is a shape with one occupant, and it will be a better shape
  for having seen two. That is a bet, and the bet is recorded here so it can be
  checked when the second one arrives.
- **No tileset rendering.** Tile ids draw as a fixed ten-colour palette, which is
  honest about what the editor currently knows, and wrong-looking the moment there is
  art. The tileset format goes with the sprite tooling.
- **No entity or trigger editing.** `map2` carries both, and this workspace edits
  neither — the layers are what a level needs first, and the rest earns its UI when a
  game reads it.
- **Cursor-anchored zoom** is not implemented (zoom is about the canvas origin), and
  `platform::set_cursor` still has no consumer: the splitter that would want it does
  not exist, because the split is fixed.
