# Chapter 30 — Save / Load & Serialization

> **What this is.** The first time the engine writes state back to disk. We turn a
> whole `Farm` — terrain, every object, the farmer — into a small text file and read
> it back *byte-for-byte identical*. Code: `src/games/iso/serialize.{hpp,cpp}` plus a
> one-function extension to the asset seam, `assets::write_file`. You will learn what
> serialization is, why a versioned text format is a great teaching choice, how to
> parse defensively, and the **transactional load** trick that never corrupts a live
> game on a bad file.

---

## 1. Concept: serialization

**Serialization** turns an in-memory object graph into a flat byte stream you can
store or send; **deserialization** rebuilds the object from those bytes. The hard part
isn't writing — it's reading *untrusted* bytes safely and reconstructing the exact
same state.

Two format families:

- **Binary** — compact and fast, but opaque and brittle (endianness, struct padding,
  field reordering). 
- **Text** — bigger and slower, but human-readable, diff-able in git, and hand-
  editable while you learn.

For a learning engine the choice is easy: **text**. You can `cat` a save file and see
your farm. We make it **versioned** (a leading `FARM 1`) so a future format change can
be detected instead of silently misread.

## 2. The format

```
FARM 1
SIZE 16 16
TILES
................          ← h lines, each exactly w chars
........#.......            '.'=grass  ':'=soil  '~'=water  '#'=path
::::: ..........
...
OBJECTS 23
T 10 2                    ← <kindChar> <x> <y>;  T R H F W
W 2 2
H 11 10
...
FARMER 7 8                ← farmer cell, or "-1 -1" if none
```

Design decisions baked in:

- **Terrain as a character grid** — one glyph per tile, `h` rows of `w` chars. Compact
  *and* visual: the save file is an ASCII map of your farm.
- **Objects as a count + list** — only the placeable kinds (`Tree Rock House Fence
  Wheat`) appear; the farmer is special and stored on its own line.
- **The farmer separately** — it isn't a "placeable", it's the agent, so it gets a
  dedicated `FARMER x y` line (`-1 -1` means none).

## 3. The byte-buffer boundary

`serialize` never touches the disk. It speaks **`std::vector<uint8_t>`**:

```cpp
std::vector<uint8_t> save_farm(const Farm& f);
bool                 load_farm(Farm& f, const std::vector<uint8_t>& bytes);
```

The *scene* bridges those bytes to storage through the asset seam. That separation is
the whole reason the round-trip is unit-testable with **no files and no SDL** — the
test saves to a buffer, loads from it, saves again, and asserts the two buffers are
identical (`tests/test_iso.cpp::test_serialize`).

## 4. Writing (`save_farm`)

Straightforward `std::ostringstream` assembly. The only subtlety is gathering objects
from the ECS while *excluding* the farmer:

```cpp
for (const Entity e : w.alive()) {
    if (e == f.farmer()) continue;
    const Renderable* r = w.renderables.get(e);
    const Position*   p = w.positions.get(e);
    if (!r || !p || r->kind == ObjKind::Farmer) continue;
    objs << objkind_char(r->kind) << ' '
         << int(std::lround(p->x)) << ' ' << int(std::lround(p->y)) << '\n';
    ++count;
}
```

`std::lround` (not a truncating cast) converts the float position back to its integer
cell — a robustness fix from code review so a position that ever arrives as `2.9999`
still saves as `3`. The final string is copied into a byte vector and returned.

## 5. Reading defensively (`load_farm`)

Parsing untrusted input is where bugs and crashes live. Every read is checked and
every value is validated:

```cpp
if (!(in >> tok >> ver) || tok != "FARM" || ver != 1) return false;          // header
if (!(in >> tok >> w >> h) || tok != "SIZE") return false;
if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;                  // sane size
if (!(in >> tok) || tok != "TILES") return false;
...
for (int y = 0; y < h; ++y) {
    std::string row;
    if (!(in >> row) || int(row.size()) != w) return false;                  // row width
    for (int x = 0; x < w; ++x) tmp.set_terrain(x, y, terrain_from(row[x]));
}
...
if (!(in >> tok >> n) || tok != "OBJECTS" || n < 0 || n > w * h) return false; // bounded
```

Three habits worth copying:

1. **Check every extraction.** `if (!(in >> x))` catches truncated/garbage files at the
   exact point the read fails.
2. **Validate ranges.** `w,h ≤ 4096`; `n ≤ w*h` (at most one object per tile). Bounding
   the count rejects an absurd hand-crafted `OBJECTS 999999999` up front — a hardening
   fix from review.
3. **Unknown glyphs degrade gracefully.** `terrain_from` maps any unexpected char to
   `Grass` rather than failing — a forgiving choice for terrain (objects, by contrast,
   reject unknown kinds).

## 6. Transactional load

The most important idea in the chapter. A naive loader mutates the live farm as it
parses — so a file that's valid for 200 lines and garbage on line 201 leaves you with
*half a farm*. We avoid that by parsing into a **temporary** and committing only on
success:

```cpp
Farm tmp(w, h);
// … fill tmp's terrain, objects, farmer …
f = std::move(tmp);   // commit: cheap move, only reached if everything parsed
return true;
```

If *any* check fails before the commit, we `return false` and the caller's farm is
**untouched** — the temporary is simply discarded. The move-assignment is O(1) (it
swaps vector/map internals), so the "transaction" costs nothing. `test_serialize`
proves it: it feeds `load_farm` the bytes `"nope"`, asserts it returns `false`, and
asserts the target farm serializes to *exactly* what it did before the failed load.

## 7. The write seam: `assets::write_file`

Reading already went through `assets::load_file` (Chapter 07). Saving needs a write
counterpart, added in the *same* seam:

```cpp
bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(g_base + "/" + path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!bytes.empty())
        f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return static_cast<bool>(f);   // false if the stream errored mid-write
}
```

Why keep it in `assets` rather than `fopen`-ing inside the scene? Because **all I/O
lives behind one seam** — the same rule that makes the web port a drop-in. On desktop
this writes a real file; at M5 the web backend can redirect persistence to the
browser's IndexedDB/localStorage in *this one function*, and no caller changes. The
scene's save handler is then trivial:

```cpp
flash(assets::write_file("farm_save.txt", save_farm(farm_)) ? "saved" : "save failed");
```

> **Web note.** Emscripten's default filesystem is in-memory and vanishes on reload.
> Real browser persistence needs IDBFS mounted and an explicit sync. That's an M5
> concern, deliberately isolated to `write_file` so it doesn't leak into the game.

## 8. Run & observe

In `--iso`: build something, press **F5** — the HUD flashes **"saved"** and
`assets/farm_save.txt` appears. `cat` it; it's your farm as ASCII. Bulldoze everything,
press **F9** — **"loaded"**, your farm returns. Corrupt the file in an editor (delete
the `SIZE` line) and press F9 — **"load failed (corrupt?)"**, and your *current* farm
is untouched, thanks to the transactional load.

## 9. Pitfalls

- **Partial writes.** Returning `true` without checking the stream state can claim a
  save succeeded when the disk was full. We return `static_cast<bool>(f)` after writing.
- **Mutating on a bad load.** The classic foot-gun; the temp-then-commit pattern is the
  cure.
- **Unbounded counts.** `OBJECTS <n>` from an untrusted file must be bounded, or a huge
  `n` invites trouble. Bound by the grid area.
- **`>>` and whitespace.** Stream extraction skips whitespace, so a terrain row must be
  a single token of exactly `w` non-space chars — which is why we check `row.size()`.
- **Locale.** `ostringstream`/`istringstream` can be locale-sensitive for numbers;
  for ASCII integers it's fine here, but worth knowing for floats.

## 10. Glossary

- **Serialization / deserialization** — object → bytes / bytes → object.
- **Versioned format** — a leading version tag so old/new files are distinguishable.
- **Transactional load** — parse into a temporary; commit only on full success.
- **Asset seam** — the single I/O choke point (`load_file`/`write_file`) that keeps the
  engine web-portable.

## 11. Exercises

1. **Round-trip fuzz.** Generate random small farms, save→load→save, assert the two
   byte buffers match. Did you find an asymmetry?
2. **Format v2.** Add per-object `tint` to the line and bump to `FARM 2`. Make the
   loader accept *both* v1 and v2 (this is why we versioned it).
3. **Corruption gauntlet.** Truncate the file at every byte offset and feed each to
   `load_farm`. It must always return cleanly (true or false), never crash.
4. **Binary variant.** Write a compact binary encoder for the same data and compare
   file sizes. What did you lose in readability?
