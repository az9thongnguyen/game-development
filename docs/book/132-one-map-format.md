# Chapter 132 — One map, one editor

> Code: `src/engine/tilemap/map_edit.{hpp,cpp}` ·
> `src/games/studio_shell/map_workspace.{hpp,cpp}` ·
> `src/engine/commands/asset_commands.cpp` · `src/games/fps/map.{hpp,cpp}` ·
> `src/main.cpp` · `assets/maps/level_00.map2` · *(deleted)* `src/games/maplab/`

## Tóm tắt (VI)

`CLAUDE.md` tự ghi món nợ này, và `main.cpp` ghi rõ **lý do** nó chưa trả được:

> `--lab map` — *"Still writes fpsmap1, still the only place entities/spawns can be
> edited. It stays until the Map workspace can do that."*

Vẽ tile đã chuyển sang Map workspace từ chương 112. **Đặt điểm xuất phát thì không.**
Nên cả một scene thứ hai sống thêm mười chương chỉ để làm đúng một việc — và trong suốt
mười chương đó nó ghi định dạng cũ.

Bốn bước, theo đúng thứ tự phụ thuộc:

1. **Thao tác còn thiếu**: `place_entity` / `set_entity_prop` trả về `doc::Command`.
   Hai quyết định được test ghim lại: **undo một lần TẠO thì xoá entity** (giữ nó lại ở
   ô đầu tiên là một chỉnh sửa khác đội lốt undo), và **kéo thì gộp, tạo thì không**.
2. **Entity tool** trong Map workspace — kéo để đặt, nút Facing để xoay, cả canvas,
   inspector lẫn palette đều gọi *một* hàm.
3. **`--cmd map.migrate`** rồi `level_00.map` → `level_00.map2`. Bằng chứng ở lại dù
   file thì không: `test_fps` nhúng **đúng những byte** file cũ mang suốt 120 chương và
   khẳng định chúng migrate ra đúng file map2 đang commit, **từng byte**.
4. **Xoá Map Lab** — và xoá luôn `fps::to_text`/`from_text`. `--lab map` vẫn còn, nhưng
   sau cánh cửa đó giờ là Map workspace full-screen. Ròng **−306 dòng**.

Và một lỗi mà chính bước 3 phát hiện ra ở bước 2: **tôi vừa viết một nút chết.**
Nút Facing ghi thuộc tính `facing` (chữ E/S/W/N); raycaster đọc `dir` (radian). Mọi
assertion đều xanh vì chúng dừng lại ở *"thuộc tính đã đổi"*.

---

## The debt, and why it did not get paid earlier

Chapter 112 built the Map workspace and it was better than Map Lab in every way that
was implemented: layers, a mask, undo, autosave, a command palette, the shared `map2`
format. Chapter 116 built `WorkspaceHost`, which is the mechanism for retiring a lab.
Chapter 120 folded twelve one-per-scene flags into `--lab <id>` and moved Map Lab
behind it.

And Map Lab stayed alive, because of one tool it had and the workspace did not:

```cpp
int tool_ = 0;   // 0 = Paint, 1 = Fill, 2 = Spawn
int facing_ = 0; // spawn facing: 0 E, 1 S, 2 W, 3 N
```

The Spawn tool. That is the whole reason. A hundred and sixty-nine lines of scene plus
a parallel edit library plus a parallel file format survived ten chapters because one
verb had not moved — and every chapter that noticed wrote it down and moved on. It is
the cheapest possible lesson about migrations: **the last 5% keeps 100% of the old
thing alive.**

## What an undo means for an entity

Painting is a stroke: a gesture that accumulates, so `Stroke` collects cells and
reaches the stack once. An entity edit is a *move*: its latest position subsumes every
earlier one, which is the shape `CommandStack` already merges by `merge_key`. So
`place_entity` returns a command with a merge key rather than accumulating.

Two decisions in there are worth more than the code:

**Undoing a creation removes the entity.** The obvious implementation restores the
previous position, and for the first placement there is none — so the entity would stay
where it was first dropped. That looks like undo working (something changed!) and is a
different edit entirely: the map keeps a spawn the author never placed, and nothing
downstream can tell it apart from one they did.

```cpp
cmd.revert = existed ? restore_position : remove_the_entity;
```

**A move merges; a creation does not.** Otherwise the drag that follows a placement
swallows the placement, and one Ctrl+Z restores a position for an entity that should
not exist at all. The same rule applies to properties, which is why cycling a facing
E → S → W is one step landing back on E, and a second press removes the facing:

```cpp
cmd.merge_key = had ? entity_key(name + "/" + key, salt) : 0;
```

The merge key hashes the entity *name*, not its index — an index moves the moment an
entity is created, which is exactly what the first edit does.

## The button I had just written was dead

Step 2 shipped a Facing button that wrote `facing=E`. Step 3 migrated the real level
and printed what came out:

```
entity spawn_player 3 8 dir=0.000000
```

`dir`. In radians. That is what `fps::from_shared_text` reads into `spawn_dir`, and it
is what the fpsmap1 migration has written since chapter 110. My button wrote a
different property, so **the game would have ignored every facing the editor set**.

Every assertion I had written was green, because every one of them stopped at *the
property changed*:

```cpp
CHECK(facing_of() == "S");    // true, and worth nothing
```

The letter is now presentation only — `dir` is stored, `facing_index()` turns it back
into a compass point for the inspector and the marker — and the test goes one step
further than the file:

```cpp
CHECK(ws.save().ok);
const auto seen = fps::from_shared_text(read_text(kPath));
CHECK(std::abs(seen->spawn_dir - kHalfPi) < 1e-4f);   // the GAME sees it
```

This is chapter 123's keyboard again (wired to a canvas nobody focused), and chapter
130's Play button (a correct `href` is not a working link). Three times now the bug has
been *the last hop*, and three times every check that stopped one hop short was green.

## The evidence outlived the file

`assets/maps/level_00.map` had been the authored level since chapter 30-something. It
is now `level_00.map2`, and `test_fps` used to prove the migration was faithful by
reading that file with both parsers — a check that dies with the file.

So the bytes moved into the test:

```cpp
static const char kLegacyLevel00[] =
    "fpsmap1\n" "size 16 16\n" ... "spawn 3 8 0.000000\n";

CHECK(tilemap::to_text(*tilemap::from_fpsmap1(kLegacyLevel00)) == committed);
```

That is *stronger* than what it replaced. The old check compared two readers of one
living file and would have passed on a file that had drifted; this one pins the exact
historical input to the exact committed output, byte for byte — the same relationship
`.hrt` has to its `.recipe` and `collection.json` to its manifests.

## Deleting the writer, not deprecating it

`tilemap::load` has migrated `fpsmap1` transparently since chapter 110. That was enough
to *read* old files and not nearly enough to stop *making* them — Map Lab kept writing
the old format for ten more chapters, under a reader that quietly cleaned up after it.

So the retirement removes producers, not just callers:

* `src/games/maplab/` — deleted (285 lines), and `test_maplab.cpp` with it.
* `fps::to_text` / `fps::from_text` — deleted. Nothing writes `fpsmap1` anywhere now.
* `tilemap::from_fpsmap1` — kept, as the one-way door, reachable through `load` and
  through the new `--cmd map.migrate <src.map> <dst.map2>`.

**A format nothing can write cannot come back.** That is why the writer had to go
rather than gain a comment saying not to use it.

`--lab map` still exists and still opens on `M`-for-map muscle memory. What is behind
the door changed: it is a `WorkspaceHost` around the same Map workspace object the
Studio's Edit section holds — the chapter-120 rule, that a lab and a tab are the same
object, applied to the last scene that was violating it.

## The screenshot found the bug again, five chapters running

And this time it was one I had introduced sixty seconds earlier. Adding the ENTITY
section pushed the inspector past the bottom of the panel, where `ui::slot()` clamps to
a **zero-height rect** — so the Save button was drawn nowhere and clickable nowhere,
silently. The Pixels workspace has reported that since chapter 127; this one did not.

`inspector_clipped()` now exists here too, and the status line says
`[panel clipped — make the window taller]`, because a control that was clipped away has
no other way to announce itself. Both directions are tested: a real 900×700 panel is
not clipped, the 400×300 one is and says so.

## What was checked

* `ctest` **77/77** green; `test_maplab` is gone with the lab it tested, and
  `test_map_edit`, `test_map_workspace`, `test_commands`, `test_fps` and
  `test_tilemap` gained cases.
* **Mutation testing, 16 single-token changes.** First run: 14 killed, and both
  survivors were the same hole — **`map.migrate` had no test at all.** Pointing it at
  a `map2` file and having it write nothing while reporting success both survived a
  full suite, because a command nobody tests is not a command that works, it is one
  nobody has contradicted. With that test: **16/16**, post-restore baseline GREEN.
* **Four tests failed the way they were supposed to.** Migrating the level made
  `collection.json` stale (the package hash moved with the content), broke two
  hard-coded paths and moved the command count. Every one of those was a guard built in
  an earlier chapter firing on a real change — which is the only evidence that they work.
* Both directions of every new guard: facing before/after an entity exists; a panel
  clipped and not clipped; a drag merging and a creation not; `map.migrate` refusing a
  file that is already `map2`.
* A rendered frame, looked at — twice. The first showed the clipped panel; the second
  shows the marker, its facing stub and the ENTITY section at a real panel size.
* Golden path re-run. The release id **moves**, and it should: the map's bytes changed.

## The harness ate a commit

Worth writing down because it is a process bug, not a code one, and it was one command
away from being permanent.

The mutation harness rewrites source files in place. It was still running when I staged
the migration work — `git add -A` picked up whatever was on disk at that instant, which
was mutation **M13** (`if (false) place_selected(...)`, the Entity tool's drag disabled),
and it went into a commit. Every test had passed minutes earlier, on files that no
longer existed.

What caught it was the harness's own last line — the one chapter 130 added:

```
sources restored clean          <- expected
 src/games/studio_shell/map_workspace.cpp | 2 +-    <- what actually printed
```

That diff is against HEAD, and after a commit landed mid-run it stopped meaning "the
harness cleaned up" and started meaning "the harness cleaned up and HEAD is wrong". It
was fixed with `--amend` before anything was pushed, and the rule is now simple: **do
not touch the index while the harness is running.** Chapter 130's lesson was that the
instrument measuring the tests needs its own check. This is the next one along: the
instrument *edits your working tree*, so it owns the tree while it runs.

## Ceilings

* **Triggers still have no editing UI.** `map2` has had them since chapter 110; the
  workspace edits tiles and entities and not those.
* **Entity properties other than `dir` cannot be edited** — only the facing cycle. An
  entity with arbitrary props round-trips through the file and is not reachable in the UI.
* **No entity deletion.** You can place and move; removing one means undo or a text
  editor.
* **The list is entities-in-the-map plus `spawn_player`.** There is no catalogue of
  entity *kinds* a map could have, because nothing else defines one yet.
* **`--lab map` opens `maps/level_00.map2` by name.** A lab has no manifest, so it
  hard-codes one path, the same way `--lab pixel` hard-codes two textures.
* **`fps::Map` is still a separate dense `uint8` grid.** Narrowing `tilemap::Map` down
  to it on load is deliberate (a DDA over an array wants an array), but it does mean
  ids above 255 clamp.
