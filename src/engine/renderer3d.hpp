// =============================================================================
//  engine/renderer3d.hpp  —  hand-written software 3D renderer (engine core)
// =============================================================================
//  Renderer3D rasterizes triangles into the SAME framebuffer the 2D renderer
//  uses (it borrows a Renderer2D for pixel writes), and owns a parallel DEPTH
//  buffer so nearer surfaces correctly hide farther ones. This is the M3 pillar:
//  the transform pipeline + z-buffered triangle fill that everything 3D reuses.
//
//  Per frame:
//      r3d.begin(sky);                         // clear color + reset depth
//      r3d.set_camera(cam.view(), cam.proj()); // view + projection matrices
//      r3d.draw_lines(grid, I);                // ground grid / axes (overlayed)
//      r3d.draw_mesh(cube, model, Mode::SolidFlat, light);
//
//  Nothing here touches SDL — it only writes pixels via Renderer2D, so it ports
//  to the web exactly like the 2D renderer does.
// =============================================================================
#pragma once

#include <vector>

#include "engine/color.hpp"
#include "engine/geometry.hpp"
#include "engine/math.hpp"
#include "engine/pipeline.hpp"
#include "engine/renderer2d.hpp"

namespace r3d {

enum class Mode {
    Wireframe,    // triangle edges only
    SolidFlat,    // one Lambert color per face (crisp facets)
    SolidGouraud, // per-vertex Lambert, perspective-correctly interpolated
};

// A single directional light. `dir` is the direction the light TRAVELS (so a
// surface is lit by how much its normal faces -dir). `ambient` is the floor.
struct Light {
    math::vec3 dir = math::normalize(math::vec3{-0.4f, -0.9f, -0.5f});
    float      ambient = 0.25f;
};

class Renderer3D {
public:
    explicit Renderer3D(gfx::Renderer2D& fb);

    void begin(gfx::Color clear);                  // clear color buffer + reset depth
    void set_camera(const math::mat4& view, const math::mat4& proj);
    void set_cull(bool on) { cull_ = on; }
    bool cull() const { return cull_; }

    // Draw a triangle-list mesh transformed by `model`, shaded per `mode`.
    void draw_mesh(const geo::Mesh& mesh, const math::mat4& model, Mode mode, const Light& light);

    // Draw a line-list mesh (grid/axes). Uses each segment's first vertex color.
    // Lines are overlaid (not depth-tested), so draw them before solid meshes.
    void draw_lines(const geo::Mesh& mesh, const math::mat4& model);

private:
    void raster_triangle(const ClipV v[3], bool gouraud);

    gfx::Renderer2D&   fb_;
    int                w_;
    int                h_;
    std::vector<float> depth_;   // w_*h_, initialised to +inf each begin()
    math::mat4         view_ = math::mat4_identity();
    math::mat4         proj_ = math::mat4_identity();
    bool               cull_ = true;
};

} // namespace r3d
