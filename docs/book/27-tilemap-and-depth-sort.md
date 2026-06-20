# Chapter 27 — The Tile Map & Depth Sorting

> **What this is.** Two ideas that travel together. First, the **tile map**: a dense
> grid of terrain, and why "dense" is the right data model for the floor. Second,
> the **painter's algorithm** with an isometric depth key — how, *without* a
> z-buffer, we still draw a tall house *in front of* the grass behind it and *behind*
> the tree in front of it. Code lives in `src/games/iso/tilemap.{hpp,cpp}` and the
> draw loop in `src/games/iso/iso_render.cpp`.

---

## 1. The tile map: a dense grid

The farm's floor is **terrain**: every cell is exactly one of grass, soil, water, or
stone path. There is no such thing as a cell with *no* terrain. That single
observation dictates the data structure:

```cpp
enum class Terrain : uint8_t { Grass, Soil, Water, Path };

class TileMap {
    int                  w_, h_;
    std::vector<Terrain> t_;   // row-major, exactly w_*h_ entries
};
```

A flat `std::vector` indexed by `y * w_ + x`. This is a **dense** structure: storage
is proportional to the grid area and every cell is always present. Lookups are O(1)
array indexing; there is no hashing, no "does this cell exist?" question.

Contrast this with the *objects* on top of the floor (trees, the farmer): there are a
handful of them scattered across hundreds of tiles. Storing those in a dense
`w*h` array would be mostly empty waste. They belong in a **sparse** structure — the
ECS of Chapter 28. **Choosing dense vs. sparse per data shape is the lesson**: the
floor is dense, the things on it are sparse.

### Bounds safety

Every accessor is bounds-checked, and out-of-range reads return a safe sentinel
rather than reading past the vector:

```cpp
Terrain TileMap::at(int x, int y) const {
    if (!in_bounds(x, y)) return Terrain::Grass;   // sentinel, never an OOB read
    return t_[static_cast<std::size_t>(y) * w_ + x];
}
bool TileMap::terrain_walkable(int x, int y) const {
    return in_bounds(x, y) && at(x, y) != Terrain::Water;   // water blocks
}
```

`screen_to_grid` (Chapter 26) happily returns cells off the edge of the map when the
mouse is outside the grid, so *every* consumer must tolerate out-of-range
coordinates. Returning `Grass` for OOB and folding `in_bounds` into
`terrain_walkable` means callers never index unsafely.

## 2. The depth problem

Here is the whole difficulty in one picture. Two objects: a tree at cell `(2,4)` and
a house at `(4,2)`. Both have `gx + gy = 6`, so they sit on the same diamond "row".
But they also have *height* — they stick up off their tile. If we draw them in the
wrong order, the far one's top pokes through the near one:

```
   WRONG (house drawn last, but it's farther back)      RIGHT
        🌳  (tree, near)                                    🏠
       🏠🌳  ← house top bleeds over tree                  🌳🏠  ← near tree on top
```

With real 3D we'd let the z-buffer sort this per pixel. In 2D isometric we have no
per-pixel depth. We have only **the order in which we paint**. Paint far things
first, near things last, and the near ones naturally cover the far ones. That is the
**painter's algorithm**.

## 3. The isometric depth key

So: in what order do we paint? We need a single number per object such that "smaller
= farther from the viewer". For isometric tiles that number is astonishingly simple:

```
depth_key(gx, gy) = gx + gy
```

Why does the *sum* work? Recall from Chapter 26 that `+gx` and `+gy` both move a tile
**down** the screen (toward the viewer). A tile is "in front of" another exactly when
it is lower on screen, i.e. when its `gx + gy` is larger. So sorting ascending by
`gx + gy` and painting in that order paints back-to-front. The viewer is, in effect,
standing off the bottom-right corner of the map looking up-left.

```
  gx+gy:   0
          1 1
         2 2 2          ← each diagonal "row" shares one depth key
        3 3 3 3            we paint row 0, then 1, then 2 … front rows last
       4 4 4 4 4
```

### The ground needs no sort

The floor tiles are flat and coplanar — they never overlap, so their order only has
to be back-to-front to avoid the 1-pixel seam fights. We get that **for free** with a
double loop:

```cpp
for (int gy = 0; gy < f.height(); ++gy)
    for (int gx = 0; gx < f.width(); ++gx)
        draw_tile(gx, gy);                 // gy outer, gx inner ⇒ key increases
```

Iterating `gy` outer, `gx` inner visits cells in non-decreasing `gx + gy` order, so
no explicit sort is needed for the ground. (Convince yourself: row `gy=0` gives keys
0..w-1; row `gy=1` gives 1..w; they interleave but never go backwards enough to
matter for coplanar tiles.)

### The objects *do* need a sort

Objects have height, so they must be painted strictly back-to-front across the whole
set. We gather them and `std::sort`:

```cpp
struct Drawable { float key; float gx, gy; ObjKind kind; };
std::vector<Drawable> ds;
for (const Entity e : world.alive()) {
    const Position*   p = world.positions.get(e);
    const Renderable* r = world.renderables.get(e);
    if (!p || !r) continue;
    ds.push_back({ depth_key(p->x, p->y), p->x, p->y, r->kind });
}
std::sort(ds.begin(), ds.end(), [](const Drawable& a, const Drawable& b) {
    if (a.key != b.key) return a.key < b.key;
    return a.gy < b.gy;                    // deterministic tie-break
});
for (const Drawable& d : ds) draw_object(/* screen pos of d */, d.kind);
```

The comparator is **lexicographic on `(key, gy)`** — first by depth, then by `gy` to
break ties deterministically. That pair-ordering is a valid *strict weak ordering*,
which is exactly what `std::sort` requires (a comparator that isn't would be
undefined behavior). The farmer, whose `(gx, gy)` is fractional while walking, slots
into the right place automatically because its key is just `gx + gy` evaluated at its
current sub-tile position.

## 4. Drawing an object with fake volume

Each object is painted by `draw_object`. The structures (rock, house, fence) use a
small helper, `draw_iso_box`, that fakes a 3D cuboid out of three flat fills:

```
        ___                top face   — brightest (shade × 1.00)
      /     \              left wall  — medium    (shade × 0.70)
     |  top  |             right wall — darkest   (shade × 0.50)
     |\     /|
     | \   / |   ← the two side walls catch progressively less "light"
     |  \ /  |
      \  |  /
        \|/
```

```cpp
void draw_iso_box(Renderer2D& g, int cx, int cy, int hw, int hh, int height, Color base) {
    // ground corners S/E/W, raised top corners Sp/Ep/Wp = ground − height
    fill_poly(g, left,  4, shade(base, 0.70f));   // down-left wall
    fill_poly(g, right, 4, shade(base, 0.50f));   // down-right wall
    fill_diamond(g, cx, cy - height, hw, hh, shade(base, 1.00f));  // top, drawn last
}
```

This is itself a *mini* painter's algorithm: the two side walls are drawn first, then
the top diamond on top of them. `shade(color, f)` just multiplies RGB by `f` and
clamps — a one-line directional-lighting cheat that makes a flat box read as solid.
Every pixel still goes through `Renderer2D` (`set_pixel`, `draw_line`); there is no
SDL drawing anywhere, true to the project's rule.

The two fill primitives are worth a glance:

- **`fill_diamond`** walks integer rows; at row `dy` the half-width tapers linearly
  to zero at the tips. The inclusive span slightly *overlaps* neighbors, which
  seamlessly tiles the ground (overlap on opaque fills is invisible; gaps would show
  the background).
- **`fill_poly`** is an even-odd **scanline polygon fill**: for each screen row,
  find where the polygon edges cross it, sort the crossings, and fill between pairs.
  It handles the slanted parallelogram side-walls that `fill_diamond` can't.

## 5. Run & observe

In `--iso`, place a tall **house** (`7`) and a **tree** (`5`) on adjacent diagonal
tiles, then pan so one is in front of the other. The near one's silhouette always
wins. Now walk the **farmer** (right-click) along a path that passes behind the house
and then in front of it — watch him vanish behind the wall and re-emerge in front,
all from that one `gx + gy` sort key. That "he goes behind the building" moment is the
payoff of the whole chapter.

## 6. Pitfalls

- **Sorting by `gx` or `gy` alone.** Neither orders the scene correctly — you need
  the *sum*. Sorting by screen-`y` also works for 1×1 tiles but breaks the instant an
  object's art is taller than a tile.
- **Invalid comparators.** A comparator that can report `a < b` *and* `b < a` (e.g.
  `a.key <= b.key`) is UB in `std::sort` and crashes in debug STLs. Keep it a strict
  weak ordering — lexicographic on a tuple is the safe pattern.
- **Multi-tile footprints.** Our sum key is provably correct only for **1×1**
  footprints. A 2×2 building can be simultaneously in front of *and* behind different
  neighbors; ordering it correctly needs a topological sort of the overlap graph. We
  deliberately keep everything 1×1 — see the exercises.
- **Ground seams.** If you switch the ground to a non-overlapping fill you may see
  1-pixel background cracks between diamonds. The overlapping integer-row fill avoids
  them.

## 7. Glossary

- **Painter's algorithm** — render back-to-front so nearer things overwrite farther
  ones; the depth-without-a-z-buffer technique.
- **Depth key** — `gx + gy`; the ascending sort value for isometric painting.
- **Dense vs. sparse storage** — array-per-cell (the floor) vs. records-for-the-few
  (the objects).
- **Strict weak ordering** — the property a `std::sort` comparator must satisfy.
- **Scanline fill** — filling a polygon row by row from edge crossings.

## 8. Exercises

1. **Visualize the key.** Tint each ground tile by `(gx+gy) % 2` (a checkerboard of
   diagonals). You'll *see* the depth rows the sort uses.
2. **Break it on purpose.** Change the comparator to sort by `gx` only and place a
   tree behind a house on the same column. Watch the artifact, then revert.
3. **2×2 house.** Give the house a 2×2 footprint and find a tree placement where the
   sum key paints it wrong. Sketch the topological-sort fix.
4. **Per-pixel honesty.** Add a tiny screen-space depth buffer (one float per pixel)
   and compare its output to the painter's algorithm. When do they disagree?
