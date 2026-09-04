# Chapter 111 — Naming every operation, and being able to take it back

> Code: `src/engine/commands/{registry,release_commands}.{hpp,cpp}` ·
> `src/engine/document/{command_stack,document}.{hpp,cpp}` ·
> `src/engine/release/ops.{hpp,cpp}` (`status`, `log`) · `src/main.cpp` (`--cmd`) ·
> Tests: `tests/test_commands.cpp`, `tests/test_document.cpp`

## Tóm tắt (VI)

Hai nền móng để Studio thôi là *cái khung* và bắt đầu thành *công cụ authoring*.

**Một là registry lệnh.** `release_ops` đã làm publish/promote/rollback gọi được từ
cả CLI lẫn Hub — nhưng nó đúng chỉ vì *có người nhớ nối cả hai chỗ*. Registry biến
điều đó thành **cấu trúc**: một thao tác đăng ký một lần dưới một id ổn định, rồi
`--cmd <id>`, command palette và nút bấm đều đi qua `cmd::run`. Thêm một thao tác
không thể chỉ thêm cho một phía nữa. Các flag cũ giờ là **alias thật sự**.

Việc nối alias lộ ngay một lỗ hổng có thật: CLI mặc định lý do thành chuỗi rỗng, nên
`--project-publish <proj>` ghi vào audit log một dòng **không có lý do**. Một dòng
trống trong sổ audit **tệ hơn là không có dòng nào** — nó trông giống bằng chứng.
Giờ mọi tham số của lệnh mutating đều bắt buộc và không được rỗng.

**Hai là undo.** `CommandStack` làm việc với *thao tác sửa*, không với *thứ bị sửa*,
nên một command chỉ là cặp closure — map, scene và pixel editor dùng chung một
lịch sử. Bốn điều đáng nhớ: `push_apply` **tự apply**; sửa mới sau khi undo **xoá
nhánh redo**; kéo slider **gộp thành một bước**; và **dirty là một vị trí, không phải
một cờ** — undo về đúng điểm đã lưu thì sạch trở lại.

Và autosave **đề nghị**, không **tự áp dụng** — tự áp dụng là cách người ta mất đúng
bản họ đã cố ý lưu.

---

## 1. A convention is not a guarantee

Chapter 97 extracted `publish`/`promote`/`rollback` out of `main.cpp` so the CLI and
the Hub scene could call the same functions. That was the right move and it worked.
But look at *why* it worked: because whoever added each operation remembered to wire
both call sites. Nothing prevented the next operation from reaching only one.

A registry converts that into structure:

```cpp
register_command(Info{"release.promote", "Promote a release forward", "", "<from> <to> <reason>"},
                 [](const std::vector<std::string>& a) { return engine::promote(a[0], a[1], a[2]); });
```

Now `--cmd release.promote`, a palette entry and a Studio button are the same call.
Adding an operation registers it everywhere at once, and forgetting to register it
means it exists nowhere — a failure that is loud instead of partial.

The old flags did not stay as a second path. They became one-line aliases:

```cpp
int promote_release(const std::string& from, const std::string& to, const std::string& reason) {
    return run_command("release.promote", {from, to, reason});
}
```

so "the CLI verb and the button are the same code" is now something the compiler
enforces rather than something the documentation asserts.

Two small design choices in the registry earn their keep:

**Re-registering an id replaces the handler.** Two handlers for one id would surface
as "sometimes the wrong thing happens", which is far worse to debug than the last
registration winning.

**An unknown id returns a failed `OpResult` naming it.** Silence would make a
mistyped command on a command line look like success.

## 2. The alias found a real hole

Routing the flags through the registry immediately exposed something the previous
slice had only half-fixed. The Studio requires a reason before it will publish. The
CLI did not: it defaulted a missing reason to the empty string, so

```sh
./build/demo --project-publish projects/creator.gameproject
```

published happily and wrote this into `releases/audit.log`:

```
1788496849 publish  development c95febd882741b29 <- c95febd882741b29
```

An audit entry with no reason is worse than no entry. It occupies a line, it carries
a timestamp, it *looks* like evidence — and it answers nothing. So every argument to
a mutating command is now required and non-empty, and the test checks each position
separately rather than only the count, because "three arguments were supplied" and
"three arguments were supplied and none of them was blank" are different claims.

CI's commands all pass real reasons and are unaffected. This is a deliberate
behaviour change to a documented CLI, made because the log is only worth keeping if
every line in it means something.

## 3. status() and log() were only ever printed

Both existed solely inside `main.cpp`, formatting straight to `stdout`. That is fine
until a window wants the same information — and then it has no choice but to
reimplement the reading, which is precisely the duplication `hub_lines` exists to
prevent one level up.

They now live in `release_ops_core` and return values:

```cpp
std::vector<ChannelStatus> status();
std::vector<AuditRecord>   log(const std::string& channel_filter = {});
```

`main.cpp` keeps its column-aligned `printf` and the command registry produces a
plainer one-per-line form. That is not duplication: the *data* comes from one place,
and presentation is the caller's job — exactly what `ops.hpp` has said since it was
written. The Release workspace can now show an audit timeline without inventing a
third reader.

One behaviour is worth naming: `log()` skips a malformed line and keeps going. A log
is evidence, and one corrupt line must not be allowed to hide the rest of it.

## 4. Undo is about edits, not about documents

`CommandStack` never learns what a document is. A command is a label plus two
closures, which is what lets a map editor, a scene editor and a pixel editor share
one history implementation instead of three.

**`push_apply` applies.** A stack you have to apply around is a stack someone
eventually forgets to apply around — and the edit that skips it does not fail
immediately, it silently breaks undo for everything below it. For the same reason a
command missing either closure is refused rather than recorded.

**A new edit after an undo clears the redo branch.** Keeping it would leave the
document with two possible futures and no way to say which one Ctrl+Y means.

**Gestures merge.** Dragging a slider emits a command per frame; without merging,
undoing a drag means sixty keystrokes. Consecutive commands sharing a non-zero key
collapse, keeping the **first** revert and the **latest** apply — so undo returns to
before the whole gesture and redo lands on its final value. Only *consecutive*: an
unrelated edit between two same-key gestures must not fuse them into one.

**Dirty is a position, not a flag:**

```cpp
bool dirty() const { return long long(done_.size()) != saved_at_; }
```

Undo back to the save point and the document is clean again. A tool that keeps
warning about changes the user has already undone is a tool whose warnings people
learn to click through — and then the one that mattered gets clicked through too.

There is a third state hiding in there. History is bounded, so trimming can throw
away the position that was saved. After that, no history index can be proven
identical to the file on disk, so the document stays **dirty** rather than claiming
to be clean. Being wrong in that direction costs a redundant save; being wrong in
the other costs the work.

## 5. Recovery is offered, never applied

Autosave is easy. What to do with the autosave on open is the part with a wrong
answer available:

```cpp
if (auto a = read_text(autosave_path(path)); a && !a->empty() && *a != out.content) {
    out.recovered = *a;
    out.state = OpenState::RecoveryOffered;
}
```

**Never applied automatically.** Silently restoring an autosave is how someone loses
the version they deliberately saved thirty seconds before the crash.

**An identical autosave prompts about nothing.** It is a leftover, and a prompt
people dismiss by reflex is worse than no prompt at all.

**An empty autosave counts as absent.** That is how `discard_autosave` marks "nothing
here", because the `assets::` seam has no delete and adding one — a function that
takes a path and removes files — is a trust-boundary decision far larger than this
convenience is worth. The ceiling it buys is stated where it is taken: a document
cannot be recovered *to* emptiness.

That rule was not foresight. The test failed: the very first `save()` called
`discard_autosave()`, which wrote an empty file, and `open()` then cheerfully offered
to recover nothing.

## What is verified, and what is not

Verified, by running it:

- 65 tests green, including every registry property (handler present, unknown id,
  argument pass-through, replacement, stable order, junk refused), each empty
  argument position separately, the full undo/redo/merge/dirty matrix including the
  trimmed-save-marker case, and autosave recovery in all four of its states.
- `--cmd` with no id lists the registered commands; `--cmd release.status` and
  `--cmd release.log production` print real data from the repository's own store.
- The old flags still behave identically, and CI's exact command lines were re-run.
- A publish with no reason is now refused (exit 1), and one with a reason still works.
- The Emscripten build still links.

Not verified:

- **Nothing in the Studio calls any of this yet.** The registry has five commands and
  no palette; `CommandStack` has no workspace pushing commands onto it; autosave has
  no timer driving it. This slice is the foundation, and the workspaces that consume
  it are the next one. Until then, undo is tested and unused — which is exactly the
  "motion without connection" the strategy warns about, and is only acceptable
  because the consumer is the very next piece of work rather than a hope.
- **Autosave has never run on a timer**, only through direct calls in a test.
- **No document has ever been recovered by a human**, only by an assertion.
