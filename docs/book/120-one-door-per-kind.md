# Chapter 120 — One door per kind of thing

> Code: `src/main.cpp` · `web/shell.html` · `CMakeLists.txt` ·
> Deleted: `src/games/hub/hub_scene.{hpp,cpp}` ·
> Docs: `CLAUDE.md`, `README.md`, `docs/PROJECT-BRIEF.md`

## Tóm tắt (VI)

`main.cpp` có **31 flag**. Mười hai trong số đó là mười hai bản sao của cùng tám dòng
`platform::Config`, mỗi bản mở một scene. Đó chính là cái mà `PLAN.md` gọi là *"29 mode
CLI rời rạc"* — bề mặt của dự án trông giống **bảng kiểm kê** chứ không giống sản phẩm.

Chương này rút xuống còn **ba loại cửa**:

| loại | cách mở | vì sao |
|---|---|---|
| **game** | `--project <manifest>` | game được **manifest** khai báo, không phải một nhánh `if` trong main |
| **Studio** | `--shell [manifest]` | một sản phẩm, một flag |
| **lab** | `--lab [id]` | demo subsystem hoặc scene chưa có manifest |

**Xoá hẳn `--hub-ui`** (và cả `hub_scene.{hpp,cpp}` — 2 file chết): Hub section của
Studio vẽ **cùng một panel**. **Gộp 12 flag** thành `--lab`: `scene map texture editor
fx light audio anim 3d viz3d iso colony`.

**Hai bảng, không phải một.** `entries()` = game (manifest khai báo được), `labs()` =
mọi thứ còn lại. Tách ra để `entry fx` trong một manifest **vẫn là điều bất khả**.

**Lỗi thật được sửa nhân tiện:** một flag lạ trước đây **rơi xuống demo M0** — gõ sai,
hoặc gõ một flag vừa bị xoá, thì mở nhầm cửa sổ và **không nói gì**. Giờ nó là lỗi
(`rc=2`) kèm danh sách đầy đủ, **và bảng "flag đã nghỉ hưu → đi đâu"**. Ai quen tay gõ
`--maplab` sẽ được chỉ đường, không bị đưa nhầm phòng.

`demo --help` in toàn bộ bề mặt. `?mode=lab-<id>` trên web cũng đi qua đúng bảng đó, nên
trang web không còn là **bản sao thứ hai** của danh sách lab.

---

## What 31 flags were made of

```cpp
if (mode == "--fx") {
    platform::Config cfg;
    cfg.title     = "hand-engine — particle fx";
    cfg.fb_width  = 960;
    cfg.fb_height = 600;
    cfg.scale     = 1;
    cfg.smooth    = true;
    cfg.highdpi   = true;
    cfg.supersample = kAA;
    return run_window(cfg, std::make_unique<fx::FxScene>());
}
```

Twelve of those, identical but for a title and a type name. One of them —
`--light` — differs in exactly one field (`supersample = 1`, because additive
per-pixel lights have no edge for SSAA to find), and that single difference is the
only thing the repetition was buying.

This is the same shape as every drift bug the last six chapters found, one step
earlier: not two copies of one idea that disagree, but twelve copies of one idea
waiting to. The fix is the one `entries()` already demonstrated in chapter 115 — one
table, and every trigger derived from it:

```cpp
v.push_back({"fx", win("hand-engine — particle fx", 960, 600, kAA), [] {
    return std::unique_ptr<engine::Scene>(new fx::FxScene());
}});
```

`--lab` with no id lists the table. It cannot list something it cannot run, and it
cannot fail to list something it can, because there is nowhere else for the answer to
come from.

## Two tables, and why they stay two

The obvious next step is to put the games in the same table. It would be wrong.

An **entry** is a name a `game.project` manifest may declare — `entry farm` — and
`known_entries()` is derived from that table so a manifest cannot name something
unlaunchable, nor a launchable thing be rejected as unknown (chapter 114 found exactly
that drift). A **lab** is not shippable: it has no manifest, no assets declared, no
release id.

One table would make `entry fx` a legal manifest. Two tables of the same shape, with
different meanings, is the honest arrangement — and `labs()` reuses `Entry`, so there
is one *struct*, two *lists*.

## What was deleted, and what was only moved

**Deleted: `--hub-ui`**, and with it `hub_scene.{hpp,cpp}`. The Studio's Hub section
draws the same `draw_hub_panel` from the same `hub_core` view model; the standalone
window was a second frame around one panel, and nothing but `main.cpp` referenced it.
Two files, gone.

**Moved, not deleted: everything else.** It is tempting to read "delete the legacy
flags" as "delete the legacy scenes", and that would have cost real things:

- `--lab map` (was `--maplab`) is still the **only** place entities and player spawns
  can be edited. The Map workspace renders and edits tiles; it has no entity UI. Until
  it does, deleting Map Lab deletes a capability.
- `--lab fx`, `light`, `audio`, `anim`, `3d`, `viz3d`, `texture` are the **only runtime
  consumers** of `particles_core`, `light_core`, `audio_core`, `tween_core`,
  `render3d_core`, `viz3d_core` and `studio_core`. Deleting them would leave seven
  libraries with tests and no user — which is the "motion without connection" the brief
  warns about, arrived at from the other direction.

So the surface shrank by thirteen flags and the project lost nothing. That distinction
is the whole point: a flag is a *door*, and the argument for removing a door is that
another one reaches the same room.

## The bug in the fall-through

Before this, an unrecognised flag fell through to the bottom of `main()`:

```cpp
// No args: the M0 engine demo (retro 480x270, nearest scaling).
```

So `demo --maplab` after this change would have opened the M0 demo. Silently. So would
`demo --projct-inspect`. A CLI that answers a question you did not ask is worse than
one that refuses, and it is *much* worse in the middle of a rename: the person most
likely to type a retired flag is the person who used it yesterday.

```cpp
if (mode.rfind("--", 0) == 0) return usage(mode);
```

`usage()` prints every mode the build answers to **and where each retired flag went**:

```
  retired (chapter 120)
    --hub-ui   -> --shell, Hub section
    --sandbox  -> --lab scene        --maplab -> --lab map
    --studio   -> --lab texture      --editor -> --lab editor
    --3d --viz3d --iso --colony --fx --light --audio --anim -> --lab <same name>
```

A rename that tells you about itself costs eight lines and replaces a search through
a changelog. `demo --help` prints the same thing on purpose; an unknown flag exits 2,
`--help` exits 0.

## The page was a second copy of the list

`web/shell.html` carried its own map of modes to arguments:

```js
colony: ['--colony'],
editor: ['--editor'],
iso:    ['--iso'],
'3d':   ['--3d'],
viz3d:  ['--viz3d'],
hubui:  ['--hub-ui', 'projects/creator.gameproject'],
```

Six of those were about to become wrong, and the page had no way to know. It went the
same way `?project=` did in chapter 118 — a passthrough rather than a list:

```js
if (mode.indexOf('lab-') === 0) argsByMode[mode] = ['--lab', mode.slice(4)];
```

`?mode=lab-fx` now reaches whatever `labs()` contains, and the page never needs editing
when the table changes.

## Documentation, thoroughly

This is a rename that touches how the project is *described*, so:

- **`CLAUDE.md`** — the operating document — has its command section rewritten around
  the three kinds of door, with the labs listed under `--lab`.
- **`README.md`**'s run block rewritten; the historical feature table keeps its rows but
  the flag names in them point at commands that exist.
- **`docs/PROJECT-BRIEF.md`** §Block 2 rewritten, with a paragraph saying what was
  folded and why; ledger rows keep their dated verdicts and gain the new names in
  parentheses (`--lab scene` (was `--sandbox`)) rather than being rewritten — a
  verification record dated 2026-09-04 should still read as what was true then.
- **`docs/book/`** is not touched. A chapter is what was true when it was written; this
  chapter is where the change is recorded, and the earlier ones keep their commands as
  historical text.

## Ceilings

- **Map Lab is still not absorbed.** It has moved doors, not homes: it still writes
  `fpsmap1`, and it is still the only editor for entities and spawns.
- **`--gui`/`--tui` stayed top-level.** Chess takes two positional arguments
  (`hvh|hvai`, `easy|medium|hard`) that the lab table has no shape for, and the TUI is
  not a window at all.
- **The labs still have no manifests.** `iso` and `colony` are games in everything but
  paperwork; giving them `.gameproject` files would move them from `labs()` to
  `entries()` and delete two more flags. `colony` generates its sprite at runtime if
  missing, which a manifest's resource closure would reject — so that is a real piece
  of work, not a rename.
- **Nothing here is covered by a test.** The CLI surface is asserted by CI's golden-path
  smoke script for the spine verbs; `--lab` and `usage()` are checked by running them.
