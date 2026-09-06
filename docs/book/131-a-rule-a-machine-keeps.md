# Chapter 131 — A rule a machine keeps

> Code: `src/engine/asset/provenance.{hpp,cpp}` · `src/engine/assets.cpp` ·
> `src/engine/commands/asset_commands.cpp` · `src/engine/paint/pixel_source.cpp` ·
> `src/games/studio_shell/pixel_workspace.{hpp,cpp}` ·
> `src/games/studio_shell/project_panel.{hpp,cpp}` · `assets/*.pack` ·
> `assets/ATTRIBUTION.md` · `CMakeLists.txt`

## Tóm tắt (VI)

`CLAUDE.md` mang một luật từ ngày cửa thứ hai mở ra: **mỗi `.hrt` mới phải có một dòng
trong `assets/ATTRIBUTION.md`, trong cùng một thay đổi.** Luật đúng. Luật không có ai
kiểm. Và nó **đã bị quên rồi** — repo có 23 file `.hrt`, file đó gọi tên **ba**. Hai mươi
cái còn lại nằm dưới một câu kết: *mọi thứ khác đều do code trong repo này sinh ra*. Câu
đó nhiều khả năng đúng. Nó **không kiểm được**, mà một khẳng định không ai kiểm được thì
không phải bằng chứng — đúng hình dạng của lời cam đoan bao trùm mà chương 129 tìm thấy
dưới 28 bài test chưa từng chạy.

Chương này thôi bắt người nhớ, và bắt máy **suy ra**:

1. **Ba cửa để lại ba dấu vết khác nhau trên đĩa.** `.pack` khai một import; một
   `.recipe` nằm cạnh nghĩa là "sinh ra"; một `.pix` nằm cạnh nghĩa là "vẽ tay". Không có
   dấu nào → `UNRECORDED`, `Ledger::ok()` thành false, test đỏ. **Boolean đó là thứ luật
   kia luôn ngụ ý và chưa bao giờ có.**

2. **`asset.new` — trần cuối cùng của "Studio là công cụ authoring" (ch.127 ghi lại).**
   Nó ghi `.pix` **trước**, rồi bake ra `.hrt`. Đó là thiết kế chứ không phải chi tiết:
   một file **sinh ra đã là source** thì được ledger gọi là `drawn` ngay từ giây đầu, và
   luật attribution không cần một ngoại lệ tên là "cái này Studio làm".

3. **Bài kiểm chưa bao giờ nhả chuột.** `ui::interact` kích hoạt khi **thả** chuột trên
   widget. Mọi test trong `test_pixel_workspace.cpp` chỉ **nhấn**. Nên chúng chứng minh
   nút *được vẽ* và chưa bao giờ chứng minh nút *bấm được* — đúng điểm mù chương 126, nằm
   dưới một bộ test trông rất kỹ. Nút `Save` đã được vẽ trong mọi khung hình của mọi test
   ở file đó và chưa từng bị bấm lần nào.

4. **Cửa import cuối cùng cũng được giữ cùng chuẩn với hai cửa kia.** `.recipe` và `.pix`
   đều được bake lại và so từng byte; bản import thì chưa — và nó là cái cửa **có giấy
   phép đứng sau**. Giờ mọi dòng `import` trong mọi `.pack` đều được chạy lại và so byte.

---

## The rule that nobody was keeping

`CLAUDE.md` says it plainly, and has since chapter 122:

> **All three** must gain a line in **`assets/ATTRIBUTION.md`** in the same change —
> imported art because of the licence, our own art because a file that is ours should be
> provably ours.

Here is what the repository actually held the morning this chapter started:

```sh
$ find assets -name '*.hrt' | wc -l
      23
$ grep -c '\.hrt' assets/ATTRIBUTION.md
       3
```

Three files named. Twenty not. They were not *hidden* — the file ends with a paragraph
that covers them:

> Every other pixel in `assets/` was produced by code in this repository — the Texture
> Lab, the sprite generators in the demos, or the Map/Scene workspaces.

That sentence is probably true. It is also the exact shape of the assurance chapter 129
pulled apart: a claim broad enough to be unfalsifiable, standing in for a check. Nobody
can run it. Nobody can tell which of the twenty it is wrong about. And the day somebody
bakes a `.hrt` from a texture pack with an attribution clause and forgets the line, the
paragraph will quietly cover that too, in the same confident voice.

A rule a human has to remember is a rule that will be forgotten. The question is not how
to remember harder; it is **what on disk already knows the answer**.

## The three doors leave three different marks

They do, and the marks were there all along:

| Door | What it leaves next to the `.hrt` | Origin |
|---|---|---|
| `asset.import` | an `import src dst` line in some `.pack` | `imported` |
| `asset.texture` | a sibling `<stem>.recipe` | `generated` |
| `asset.pixels` | a sibling `<stem>.pix` | `drawn` |
| — | nothing | **`UNRECORDED`** |

`engine::attribute` is that table as a pure function: every `.hrt`, every source file,
every parsed pack in; a `Ledger` out. `Ledger::ok()` is false when anything is
unrecorded — and that boolean is what the rule always implied and never had.

The fourth row is the whole point. A ledger that can only report success is the
paragraph it replaces, so the tests are mostly negative: a file with no source, a pack
claiming a file that is not there, and a file with **two** origins. That last one is not
a precedence puzzle to resolve quietly — two answers to *who is answerable for this
picture* means the answer is unknown, and it is reported as a problem rather than
silently ranked.

## `list_tree`, and the folder nobody would have walked

The seam gained a fourth verb. `list_dir` (chapter 130) answers *what is in this folder*;
this answers *what is in this subtree*:

```cpp
std::vector<std::string> list_tree(const std::string& dir, const std::string& suffix = "");
```

The difference is whether a caller can be **complete**. The obvious ledger walks
`textures/`, because that is where textures live. It would have reported a clean sheet
while thirteen chess pieces sat in `pieces/`, a sprite sheet in `sprites/`, and
`colony_agent.hrt` at the asset root — fourteen of the twenty holes, invisible.

That is the same shape as chapter 128's preload denylist: what got through was the line
nobody added. So the scan walks everything and the test asserts it saw all three places,
because "it found some files" and "it found the files" are different claims.

## Twenty files, named one by one

The twenty with no surviving source are not pretended into a door they did not come
through. They are `declared` — listed individually in `assets/ours.pack`:

```
assetpack1
name Ours (pre-doors)
note Generated by code here, but no committed source survives to re-bake them from.
     That is a weaker claim than the three doors make and is written down as such.
file pieces/wK.hrt
...
```

`declared` reads as what it is in the table: a promise, not a bake. The honest version of
the sentence it replaces — same content, twenty times more specific, and now a **closed
set**, so the twenty-first file to arrive without a source is a red test rather than
another thing the paragraph absorbs.

## The ledger is spliced, not owned

`ATTRIBUTION.md` keeps its prose. Why a tile exists, whose palette it borrows, what a
`.recipe` means — a person writes that and should. Only the **table** is generated,
between two markers:

```
<!-- BEGIN LEDGER (generated) -->
...
<!-- END LEDGER (generated) -->
```

`splice_ledger` replaces exactly that region and **refuses** a document with no markers.
Appending would give the file two ledgers, and the file it would do that to is one
somebody wrote. It is idempotent, which is what lets `test_provenance` re-bake the whole
thing and compare **bytes** — the same relationship `.hrt` has to its `.recipe`, and
`assets/collection.json` to the manifests it indexes (chapter 130).

## Bringing a sheet into existence

Chapter 127 wrote the ceiling down at the end of the pixel editor's chapter: the Pixels
workspace edits the textures a manifest **already declares**. It could change art. It
could not add any. Making one tile meant a text editor, a bake command, a manifest line
and an attribution line — four steps in three places, and three of them easy to forget.

`asset.new` is the whole act:

```sh
./build/demo --cmd asset.new fence 16 1 1 projects/farm.gameproject
# created textures/fence.pix -> textures/fence.hrt  (16x16);
#   declared in projects/farm.gameproject; ledger re-baked
```

It writes the **`.pix` first** and bakes from it. That is not an implementation detail
and it is worth being explicit about: a blank `.hrt` written straight to disk would
arrive with **no origin at all** — the exact hole the ledger refuses — and would be
uneditable as anything but pixels. A file born as a source is `drawn` from its first
second, and the previous section's rule needs no special case for the Studio.

The name becomes a path, so it is a trust boundary: letters, digits, `_` and `-`, which
rules out `..` without having to reason about it. The size cap is checked by **division**
before the multiply that would overflow; after it, a bad number is a several-gigabyte
allocation rather than an error message.

### The half-done creation

The first draft validated the manifest **after** writing the files. So:

```
$ demo --cmd asset.new x 16 1 1 nosuch.gameproject
created textures/x.pix -> textures/x.hrt (16x16); but cannot read nosuch.gameproject
```

Failure reported, two real files left behind, and a ledger now stale — which means the
next `ctest` run goes red for a reason that has nothing to do with what you were doing.
A creation that half happened is worse than one that did not happen at all.

Everything that can be refused is now refused before anything is written. Same lesson the
release store already carries as stage-then-rename; it just had not reached this file.

## The tests had never let go of the mouse

The Studio side is a name field and a Create button. Writing its test found something
that had been true for three chapters:

```cpp
bool Context::interact(Id id, Rect r, bool enabled) {
    ...
    if (active_ == id) {
        if (in_.released) { activated = over; active_ = 0; }   // <- HERE
    } else if (over && in_.pressed) {
        active_ = id;
```

A `ui::button` activates on **release over the rect**. And the helper every test in
`test_pixel_workspace.cpp` used to synthesize a mouse:

```cpp
ui::Input mouse(int x, int y, bool down, bool pressed) {
    ui::Input u{};
    u.mx = x; u.my = y; u.down = down; u.pressed = pressed;
    return u;                                    // `released` is never set. By anyone.
}
```

So the suite could drag a slider (which reads `down`) and type into a field (which reads
`pressed` for focus) and had **never pressed a button**. Every button in that inspector —
four tools, Undo, Redo, Save — was drawn in every frame of every test and clicked in none
of them. They proved the widgets were *drawn*. Not one asked whether a drawn control can
be *pressed*.

That is chapter 126's blind spot exactly, sitting under a suite that looked thorough.
There is now a `click()` helper that presses, releases, and runs a frame; Create is driven
through it end to end — type into the real field, press the real rect the inspector
published — and `Save` is finally clicked too. It works. That is not the point; the point
is that nothing in this repository could have told me either way.

## The import door, finally held to the same standard

`.recipe` and `.pix` are each re-baked and byte-compared, and both chapters say so
proudly. The import never was. It was the one door whose output had to be taken on trust,
which is an odd place for the trust to sit — it is the door with a **licence** behind it.

`test_commands` now reads the `import` lines out of the packs and re-runs each one:

```cpp
for (const auto& imp : pack->imports) {
    const auto committed = assets::load_file(imp.second);
    cmd::run("asset.import", {imp.first, "textures/_reimport.hrt"});
    CHECK(*committed == *assets::load_file("textures/_reimport.hrt"));
}
CHECK(checked >= 1);   // a loop over an empty list passes every assertion inside it
```

It reads the claims rather than naming Kenney, so a second pack is covered the day it is
added and not the day somebody remembers to widen a test. The last line is there because
"every import reproduces" is trivially true of zero imports, and a check that can pass by
finding nothing is not a check.

## What the card says

The Project section's asset detail gained ORIGIN / FROM / LICENCE. The ledger arrives as
a **separate argument** rather than living inside `InspectedAsset`, and the reason is a
cost: `engine::inspect()` runs on every launch, package and publish, and a walk of the
whole asset tree does not belong in any of them. Two questions, two shapes, one panel.

The golden test carries the negative control that makes the screenshot mean anything: an
**empty** ledger must draw a different card. Without that line, "the origin is on screen"
would pass just as happily on a panel that never read the ledger at all.

## What was checked

* `ctest` **78/78** green (`provenance` is new; `paint`, `commands`, `pixel_workspace`
  and `shell_golden` gained cases).
* **Mutation testing, 20 single-token changes** across `provenance.cpp`,
  `pixel_source.cpp`, `asset_commands.cpp` and `pixel_workspace.cpp`. First run:
  **14 killed**, and all four fixable survivors were real holes. The best of them:
  `asset.new` overwriting an existing `.hrt` survived, because the `.pix` check fires
  first — but **twenty of this repository's twenty-three rasters are a `.hrt` with no
  `.pix`**, including the imported CC0 sheet, so that guard is the only thing standing
  between `asset.new town …` and Kenney's tileset. And "the Create button is always
  enabled" survived because the *function* refuses anyway; the only observable
  difference is that a wrongly-enabled button **says** an error nobody asked for, so
  the assertion that kills it is `take_message()` being empty. Final: **18/20**, with a
  printed post-restore baseline (chapter 130's fix) reading GREEN. The two survivors are
  documented rather than chased: `size < 1` → `size < 0`, and the size cap written as a
  multiply instead of a division. Both are the same guard, and on arm64 they are
  indistinguishable — division by zero does not trap and the multiply does not overflow
  at any magnitude that would not also hang the suite. The guard is a division because a
  division cannot overflow; no test here proves that.
* **A rendered frame, looked at** — the inspector with NEW SHEET and the asset card with
  ORIGIN / FROM / LICENCE, rendered offscreen and converted to PNG.
* **Both directions of every new guard**: `new_sheet` refused while dirty *and* allowed
  after a save; Create disabled with no name *and* enabled with one; `splice_ledger`
  refusing a document with no markers *and* replacing the region in one that has them;
  a `.hrt` with no source *and* the same file once declared.
* Golden path re-run: inspect → publish → verify → promote → status → hub, exit 0, zero
  `.tmp` files, and the package hash **unchanged** (`cd1c2864f8315bff`) — the ledger adds
  no bytes to a release.
* Emscripten web build green.

## Ceilings

* **A new sheet is 16 px, one tile.** The command takes any size; the button does not.
  A sheet's *grid* is a design decision and the inspector has one text field.
* **A new sheet opens with an empty palette** — the palette is sampled from the image and
  a blank image has no colours. The mixer (chapter 127) is the only way to pick one, which
  is exactly what it was built for, but it means the first click on a fresh sheet paints
  white unless you go there first.
* **`declared` is a promise.** Twenty files still cannot be re-baked from anything. The
  ledger says so rather than fixing it.
* **The ledger is baked by hand.** `asset.new` re-bakes it; `asset.import`,
  `asset.texture` and `asset.pixels` do not. The test catches staleness afterwards, which
  is the same ceiling `collection.json` carries.
* **Only `.hrt` is tracked.** Fonts, maps, `.def` files and scenes have origins too and
  no ledger. Fonts are named in prose; the rest are ours by construction.
* **No `.pack` importer.** A pack is a hand-written file; nothing downloads, unzips or
  verifies a checksum. The `import` lines are re-run and byte-compared, which covers the
  half that matters for reproducibility and none of the half about provenance of the ZIP.
* **The tool buttons are still never clicked.** The `click()` helper exists and `Save`
  uses it; the four tool buttons do not publish their rects, so they remain drawn-and-
  unpressed in tests. One more control's worth of the same blind spot.
