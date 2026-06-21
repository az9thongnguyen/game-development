# Chapter 20 — Geometry: Meshes & Primitive Generators

> **Where we are.** Chapters 18–19 can transform and fill *one* triangle. But scenes
> are made of thousands of triangles organized into shapes. This chapter is about the
> data structure that holds a shape — the **mesh** — and the small functions that
> *generate* the shapes we need (cube, plane, sphere, grid, axes) so we never hand-type
> a vertex list. It also covers **normals**, the per-vertex vectors that lighting
> (Chapter 22) depends on.

---

## 1. Vertices and indices

A mesh is two arrays. The first is the **vertices** — the unique corner points:

```cpp
struct Vertex {
    math::vec3 pos;                          // position in model space
    math::vec3 normal{};                     // outward surface direction (lighting)
    gfx::Color color = gfx::colors::white;   // albedo / line color
};
```

The second is the **index array** — small integers that say *which* vertices form each
primitive. This indirection is the key idea: a cube has only **8 corners**, but its
12 triangles reference those corners **36 times**. Without indices we'd store (and
re-transform) the same corner three or four times.

```cpp
struct Mesh {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;   // triangle list (groups of 3) OR line list (groups of 2)
};
```

We use the index array two ways:

- **Triangle list** — indices in groups of **3**; each triple is a filled triangle.
  (`make_cube`, `make_plane`, `make_sphere`.)
- **Line list** — indices in groups of **2**; each pair is a line segment.
  (`make_grid`, `make_axes`, drawn with `Renderer3D::draw_lines`.)

This is exactly the vertex/index split every GPU uses (`glDrawElements`).

---

## 2. Winding: the rule that makes faces face outward

For backface culling (Chapter 22) to work, **all front faces must wind the same way**
— we choose **counter-clockwise when viewed from outside**. Get this wrong on one face
and that face vanishes (it's treated as a back face). So generators must be deliberate
about vertex order.

Take the cube's front face (the `+Z` side). Viewed from `+Z` with `+x` right and `+y`
up, the corners go counter-clockwise as `v4 → v5 → v6 → v7`:

```
   v7──────v6        front face (z = +h), seen from +Z:
   │        │        v4 bottom-left, v5 bottom-right,
   │        │        v6 top-right,  v7 top-left
   v4──────v5        triangles (4,5,6) and (4,6,7) — both CCW
```

You can verify any face's winding by computing its geometric normal
`(b−a) × (c−a)` and checking it points *away* from the cube's center. The cube
generator lists all six faces this way:

```cpp
auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    m.indices.insert(m.indices.end(), {a, b, c, a, c, d});   // two CCW triangles
};
quad(4, 5, 6, 7);  // front  (+z)      quad(5, 1, 2, 6);  // right (+x)
quad(1, 0, 3, 2);  // back   (-z)      quad(7, 6, 2, 3);  // top   (+y)
quad(0, 4, 7, 3);  // left   (-x)      quad(0, 1, 5, 4);  // bottom(-y)
```

---

## 3. Normals: which way a surface faces

A **normal** is a unit vector perpendicular to a surface; lighting needs it to know how
directly a face points at the light. There are two kinds:

- **Face normal** — one per triangle, computed from its three positions
  `normalize((b−a) × (c−a))`. Used by **flat** shading.
- **Vertex normal** — one per vertex, the *average* of the face normals of the
  triangles that touch it. Used by **Gouraud** shading for smooth surfaces.

`recompute_normals` builds smooth vertex normals by accumulating each face's normal
(left un-normalized so it's **area-weighted** — bigger faces count more) onto its
corners, then normalizing:

```cpp
for (auto& v : mesh.vertices) v.normal = {0,0,0};
for (each triangle a,b,c) {
    math::vec3 fn = math::cross(b.pos - a.pos, c.pos - a.pos);  // area-weighted
    a.normal += fn;  b.normal += fn;  c.normal += fn;
}
for (auto& v : mesh.vertices) v.normal = math::normalize(v.normal);
```

> **The cube's "rounded" Gouraud look — by design.** Because our cube shares 8 corners
> between 3 faces each, every corner's averaged normal points *diagonally* outward
> `(±1,±1,±1)`. Under **flat** shading the face normal is recomputed per triangle, so
> the cube looks crisply faceted (correct). Under **Gouraud** shading those diagonal
> vertex normals make the cube look slightly *rounded* — a perfect live demonstration
> of Gouraud's weakness on hard edges. A truly faceted Gouraud cube would need **24**
> vertices (4 per face) with exact face normals; we keep 8 on purpose so the contrast
> is visible. (The sphere, which really is smooth, gets exact normals instead — see
> below.)

---

## 4. The generators, briefly

- **`make_cube(size, color)`** — 8 corners, 12 triangles, smooth normals (see above).
- **`make_plane(size, subdiv, color)`** — a flat grid of `subdiv²` cells in the `y=0`
  plane, all normals `+Y`. A handy floor.
- **`make_sphere(radius, stacks, slices, color)`** — a **UV sphere**: walk latitude
  (`stacks`, 0..π) and longitude (`slices`, 0..2π), place each vertex at
  `r·(sinφ·cosθ, cosφ, sinφ·sinθ)`. The normal is just the **normalized position**
  (exact for a sphere — no averaging needed). Index count is `stacks·slices·6`.
- **`make_grid(half, divisions, color)`** — a line list: one line along X and one
  along Z at each division, for the ground reference.
- **`make_axes(length)`** — three colored line segments from the origin: **+X red,
  +Y green, +Z blue**. The universal orientation gizmo.

Each is unit-tested in `test_render3d` for the right counts and normal properties (e.g.
"every cube normal is unit length and points away from the center"; "every sphere
normal equals `normalize(pos)`").

---

## 5. Worked example: the sphere's vertex count

`make_sphere(1, stacks=8, slices=12)` builds a grid of `(stacks+1)·(slices+1) = 9·13 =
117` vertices (the extra row/column duplicate the seam — needed if you later add
per-vertex UVs that must wrap; our `Vertex` has none yet, so the duplicates are
harmless here and keep the grid topology uniform).
The index count is `stacks·slices·6 = 8·12·6 = 576` — that's `8·12 = 96` quads, each
two triangles, each triangle 3 indices. `test_geometry` asserts exactly this.

---

## 6. Pitfalls

- **Inconsistent winding.** One mis-wound face disappears under culling. When a face
  is mysteriously missing, suspect its vertex order first.
- **Shared vs split vertices.** Smooth shapes (sphere) want *shared* vertices for
  smooth normals; hard-edged shapes (a faceted cube) want *split* vertices. Choose per
  shape; don't expect one mesh to do both well.
- **Pole degeneracy.** A UV sphere's top/bottom rows collapse to a point, making
  thin/zero-area triangles near the poles. Harmless here (they rasterize to nothing),
  but worth knowing.
- **Normals not normalized after transform.** A model matrix with scale changes normal
  length; always re-normalize after transforming (Chapter 22).

---

## 7. Glossary

- **Mesh** — vertices + indices describing a shape.
- **Index buffer** — integers grouping vertices into triangles (3s) or lines (2s).
- **Winding order** — the direction (CW/CCW) vertices are listed; defines front vs
  back face.
- **Face normal** — perpendicular of one triangle (flat shading).
- **Vertex normal** — averaged perpendicular at a corner (smooth/Gouraud shading).
- **UV sphere** — a sphere meshed by latitude/longitude bands.

## 8. Exercises

1. **Add a primitive.** Write `make_cylinder(radius, height, slices)`. Decide its
   winding and verify it renders solid with culling on.
2. **Faceted cube.** Make a 24-vertex cube with exact face normals and compare its
   Gouraud look to the 8-vertex one. Which looks "more cube"?
3. **Crank the sphere.** Render `make_sphere` at `stacks=4, slices=6` vs `40, 60`. Where
   do you first notice the facets disappear?
4. **Normal visualizer.** Temporarily draw each vertex normal as a short line
   (`pos → pos + 0.2·normal`) using `draw_lines` to *see* the smooth vs averaged
   normals on the cube and sphere.
