// =============================================================================
//  engine/geometry.hpp  —  meshes + primitive generators (engine core)
// =============================================================================
//  A Mesh is the engine's unit of 3D shape: a flat array of vertices plus an
//  index array that stitches them into primitives. Two interpretations of the
//  index array are used in M3:
//
//    * TRIANGLE LIST  — indices come in 3s; each triple is a filled triangle.
//                       Used by make_cube / make_plane / make_sphere.
//    * LINE LIST      — indices come in 2s; each pair is a line segment.
//                       Used by make_grid / make_axes (drawn with draw_lines).
//
//  Indexed meshes matter: a cube has 8 corners but 36 triangle indices, so the
//  same 8 transformed positions are reused 36 times instead of recomputed. This
//  is the same vertex/index split every GPU uses.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "engine/color.hpp"
#include "engine/math.hpp"

namespace geo {

struct Vertex {
    math::vec3 pos;
    math::vec3 normal{};                      // outward surface normal (for lighting)
    gfx::Color color = gfx::colors::white;    // albedo / line color
};

struct Mesh {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;   // triangle list (3s) OR line list (2s)
};

// ---- Triangle primitives ----------------------------------------------------
// A unit-ish cube of edge `size`, centered at the origin. 8 shared vertices, 12
// triangles (36 indices); per-vertex normals are the averaged corner normals.
Mesh make_cube(float size = 1.0f, gfx::Color color = gfx::colors::white);

// A flat square in the y=0 plane, `size` across, split into subdiv*subdiv cells.
// All normals point +Y. Good as a floor.
Mesh make_plane(float size, int subdiv, gfx::Color color = gfx::colors::white);

// A UV sphere of `radius` with `stacks` latitude bands and `slices` longitude
// segments. Normals are exact (normalized position). stacks*slices*6 indices.
Mesh make_sphere(float radius, int stacks, int slices, gfx::Color color = gfx::colors::white);

// ---- Line primitives (drawn via Renderer3D::draw_lines) ---------------------
// A ground grid in the y=0 plane: `divisions` cells across `2*half_extent`.
Mesh make_grid(float half_extent, int divisions, gfx::Color color = gfx::rgb(70, 70, 80));

// The three coordinate axes from the origin: +X red, +Y green, +Z blue.
Mesh make_axes(float length);

// ---- Helpers ----------------------------------------------------------------
// Recompute smooth per-vertex normals for a TRIANGLE-LIST mesh: each face's
// (area-weighted) normal is accumulated onto its vertices, then normalized.
void recompute_normals(Mesh& mesh);

} // namespace geo
