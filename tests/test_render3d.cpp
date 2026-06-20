// =============================================================================
//  tests/test_render3d.cpp  —  3D pipeline + geometry unit tests (CTest)
// =============================================================================
//  Dependency-free: it exercises the PURE pipeline math (engine/pipeline.hpp)
//  and the geometry generators (engine/geometry.hpp) with no window and no SDL.
//  The framebuffer-touching rasterizer is verified separately via a headless run.
// =============================================================================
#include "engine/geometry.hpp"
#include "engine/math.hpp"
#include "engine/pipeline.hpp"

#include <cmath>
#include <cstdio>

using namespace r3d;
using math::vec2;
using math::vec3;
using math::vec4;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(a) + std::fabs(b));
}

// ---- to_screen: NDC -> pixel mapping, with the y-flip and [0,1] depth ----
static void test_to_screen() {
    const Screen c = to_screen(vec4{0, 0, 0, 1}, 200, 100);  // NDC origin
    CHECK(approx(c.x, 100.0f));   // horizontal center
    CHECK(approx(c.y, 50.0f));    // vertical center
    CHECK(approx(c.depth, 0.5f)); // mid-depth

    const Screen top = to_screen(vec4{0, 1, 0, 1}, 200, 100);  // NDC y = +1 (up)
    CHECK(approx(top.y, 0.0f));   // maps to the TOP row (y flipped)

    const Screen near = to_screen(vec4{0, 0, -1, 1}, 200, 100); // NDC z = -1
    CHECK(approx(near.depth, 0.0f));  // near plane -> depth 0
}

// ---- full MVP: project world points through a real camera ----
static void test_mvp_projection() {
    const math::mat4 view = math::mat4_look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const math::mat4 proj = math::mat4_perspective(math::radians(60.0f), 2.0f, 0.1f, 100.0f);
    const math::mat4 vp = proj * view;
    const int W = 400, H = 200;

    const Screen o = to_screen(vp * vec4{0, 0, 0, 1}, W, H);
    CHECK(approx(o.x, 200.0f, 1e-3f));  // world origin -> screen center
    CHECK(approx(o.y, 100.0f, 1e-3f));
    CHECK(o.depth > 0.0f && o.depth < 1.0f);

    const Screen right = to_screen(vp * vec4{1, 0, 0, 1}, W, H);
    CHECK(right.x > 200.0f);            // +x world -> right of center

    const Screen up = to_screen(vp * vec4{0, 1, 0, 1}, W, H);
    CHECK(up.y < 100.0f);              // +y world -> above center (smaller screen y)
}

// ---- winding / backface: reversing two vertices flips the signed area sign ----
static void test_signed_area_winding() {
    const float ccw = signed_area({0, 0}, {1, 0}, {0, 1});
    const float cw  = signed_area({0, 0}, {0, 1}, {1, 0});
    CHECK(ccw > 0.0f);
    CHECK(cw < 0.0f);
    CHECK(approx(ccw, -cw));
}

// ---- barycentric weights ----
static void test_barycentric() {
    const vec2 a{0, 0}, b{4, 0}, c{0, 4};
    const vec3 w = barycentric(a, b, c, vec2{4.0f / 3.0f, 4.0f / 3.0f});  // centroid
    CHECK(approx(w.x, 1.0f / 3.0f));
    CHECK(approx(w.y, 1.0f / 3.0f));
    CHECK(approx(w.z, 1.0f / 3.0f));
    CHECK(approx(w.x + w.y + w.z, 1.0f));

    const vec3 out = barycentric(a, b, c, vec2{5, 5});  // outside the triangle
    CHECK(out.x < 0.0f || out.y < 0.0f || out.z < 0.0f);

    const vec3 deg = barycentric(a, a, a, vec2{0, 0});   // degenerate
    CHECK(deg.x < 0.0f);
}

// ---- near-plane clipping ----
static void test_clip_near() {
    ClipV out[2][3];

    // (a) all three vertices in front of the near plane -> unchanged single tri.
    ClipV front[3] = {{{0, 0, 0, 1}, 0}, {{1, 0, 0, 1}, 0}, {{0, 1, 0, 1}, 0}};
    CHECK(clip_near(front, out) == 1);
    CHECK(approx(out[0][0].clip.x, 0.0f));
    CHECK(approx(out[0][1].clip.x, 1.0f));

    // (b) all behind the near plane (w + z < 0) -> nothing.
    ClipV behind[3] = {{{0, 0, -2, 1}, 0}, {{1, 0, -2, 1}, 0}, {{0, 1, -2, 1}, 0}};
    CHECK(clip_near(behind, out) == 0);

    // (c) one vertex behind -> a quad -> two triangles; every output vertex must
    //     satisfy w + z >= 0 (i.e. on or in front of the near plane).
    ClipV straddle[3] = {{{0, 0, 0, 1}, 0}, {{1, 0, 0, 1}, 0}, {{0, 1, -2, 1}, 0}};
    const int n = clip_near(straddle, out);
    CHECK(n == 2);
    for (int t = 0; t < n; ++t)
        for (int v = 0; v < 3; ++v)
            CHECK(out[t][v].clip.w + out[t][v].clip.z >= -1e-5f);
}

// ---- color interpolation used by clipping / Gouraud ----
static void test_lerp_color() {
    const gfx::Color mid = lerp_color(gfx::colors::black, gfx::colors::white, 0.5f);
    CHECK(gfx::r_of(mid) >= 127 && gfx::r_of(mid) <= 128);
    CHECK(gfx::b_of(mid) >= 127 && gfx::b_of(mid) <= 128);
    CHECK(lerp_color(gfx::colors::red, gfx::colors::green, 0.0f) == gfx::colors::red);
}

// ---- geometry generators ----
static void test_geometry() {
    // Cube: 8 shared corners, 12 triangles (36 indices); normals unit + outward.
    const geo::Mesh cube = geo::make_cube(2.0f);
    CHECK(cube.vertices.size() == 8);
    CHECK(cube.indices.size() == 36);
    for (const auto& v : cube.vertices) {
        CHECK(approx(math::length(v.normal), 1.0f));
        CHECK(math::dot(v.normal, v.pos) > 0.0f);  // points away from the center
    }

    // Plane: (subdiv+1)^2 verts, subdiv^2*2 triangles, all normals +Y.
    const geo::Mesh plane = geo::make_plane(4.0f, 2, gfx::colors::white);
    CHECK(plane.vertices.size() == 9);
    CHECK(plane.indices.size() == 2 * 2 * 6);
    for (const auto& v : plane.vertices) CHECK(approx(v.normal.y, 1.0f));

    // Sphere: index count = stacks*slices*6; normals unit and == normalize(pos).
    const geo::Mesh sph = geo::make_sphere(1.0f, 8, 12);
    CHECK(sph.indices.size() == static_cast<size_t>(8 * 12 * 6));
    for (const auto& v : sph.vertices) {
        CHECK(approx(math::length(v.normal), 1.0f, 1e-3f));
        const math::vec3 nf = math::normalize(v.pos);
        CHECK(approx(v.normal.x, nf.x, 1e-3f));
    }

    // Grid + axes are LINE lists (index count divisible by 2).
    const geo::Mesh grid = geo::make_grid(5.0f, 4);
    CHECK(grid.indices.size() == static_cast<size_t>(4 * (4 + 1)));  // 2 lines per division line
    CHECK(grid.indices.size() % 2 == 0);
    const geo::Mesh axes = geo::make_axes(1.0f);
    CHECK(axes.vertices.size() == 6);
    CHECK(axes.indices.size() == 6);
}

int main() {
    test_to_screen();
    test_mvp_projection();
    test_signed_area_winding();
    test_barycentric();
    test_clip_near();
    test_lerp_color();
    test_geometry();

    if (g_failures == 0) std::printf("render3d: all tests passed\n");
    else                 std::printf("render3d: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
