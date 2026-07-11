// =============================================================================
//  tests/test_render3d.cpp  —  3D pipeline + geometry unit tests (CTest)
// =============================================================================
//  Dependency-free: it exercises the PURE pipeline math (engine/pipeline.hpp)
//  and the geometry generators (engine/geometry.hpp) with no window and no SDL.
//  The framebuffer-touching rasterizer is verified separately via a headless run.
// =============================================================================
#include "engine/camera.hpp"
#include "engine/geometry.hpp"
#include "engine/math.hpp"
#include "engine/pipeline.hpp"
#include "engine/renderer2d.hpp"
#include "engine/renderer3d.hpp"
#include "platform/platform.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

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

// ---- 2D segment clipping (keeps the line int-cast in range, slope preserved) ----
static void test_clip_segment() {
    // Fully inside: unchanged, returns true.
    float x0 = 5, y0 = 5, x1 = 15, y1 = 15;
    CHECK(clip_segment(x0, y0, x1, y1, 20, 20));
    CHECK(approx(x0, 5.0f) && approx(x1, 15.0f));

    // Crosses the left edge: x0 clamped to 0, slope preserved (horizontal line).
    x0 = -10; y0 = 5; x1 = 5; y1 = 5;
    CHECK(clip_segment(x0, y0, x1, y1, 20, 20));
    CHECK(approx(x0, 0.0f) && approx(y0, 5.0f));

    // Huge coordinate (the pathological near-plane case): clipped into range.
    x0 = 500000.0f; y0 = 10; x1 = 10; y1 = 10;
    CHECK(clip_segment(x0, y0, x1, y1, 64, 64));
    CHECK(x0 <= 63.0f && x0 >= 0.0f);

    // Fully outside (both left of the rect): returns false.
    x0 = -50; y0 = 5; x1 = -10; y1 = 5;
    CHECK(!clip_segment(x0, y0, x1, y1, 20, 20));
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
    // Cube: 24 per-face verts (4/face, flat normals so it stays crisp in Gouraud),
    // 12 triangles (36 indices); normals unit + outward.
    const geo::Mesh cube = geo::make_cube(2.0f);
    CHECK(cube.vertices.size() == 24);
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

    // Cylinder: side + two caps; slices*12 indices; normals unit length.
    const geo::Mesh cyl = geo::make_cylinder(0.8f, 1.6f, 24);
    CHECK(cyl.indices.size() == static_cast<size_t>(24 * 12));
    for (const auto& v : cyl.vertices) CHECK(approx(math::length(v.normal), 1.0f, 1e-3f));

    // Grid + axes are LINE lists (index count divisible by 2).
    const geo::Mesh grid = geo::make_grid(5.0f, 4);
    CHECK(grid.indices.size() == static_cast<size_t>(4 * (4 + 1)));  // 2 lines per division line
    CHECK(grid.indices.size() % 2 == 0);
    const geo::Mesh axes = geo::make_axes(1.0f);
    CHECK(axes.vertices.size() == 6);
    CHECK(axes.indices.size() == 6);
}

// ---- rasterizer: drives a real framebuffer through Renderer3D (no SDL) ----
static geo::Mesh single_tri(float z, gfx::Color col) {
    geo::Mesh m;
    m.vertices = {
        {{-0.6f, -0.6f, z}, {0, 0, 1}, col},
        {{ 0.6f, -0.6f, z}, {0, 0, 1}, col},
        {{ 0.0f,  0.6f, z}, {0, 0, 1}, col},
    };
    m.indices = {0, 1, 2};
    return m;
}

static void test_rasterizer_depth() {
    const int W = 64, H = 64;
    std::vector<uint32_t> buf(static_cast<size_t>(W * H));
    platform::Framebuffer fb{buf.data(), W, H, W};
    gfx::Renderer2D r2(fb);
    r3d::Renderer3D r3;
    r3.set_cull(false);  // identity projection: don't reason about winding here
    r3.set_camera(math::mat4_identity(), math::mat4_identity());
    const r3d::Light light;
    const math::mat4 I = math::mat4_identity();
    auto center = [&]() { return buf[static_cast<size_t>(H / 2) * W + W / 2]; };

    // Coverage: a centered triangle paints the center pixel.
    r3.begin(r2, gfx::colors::black);
    r3.draw_mesh(single_tri(0.0f, gfx::colors::white), I, r3d::Mode::SolidFlat, light);
    CHECK(center() != gfx::colors::black);

    // Depth: near green (z=-0.5) beats far red (z=+0.5) — far drawn FIRST.
    r3.begin(r2, gfx::colors::black);
    r3.draw_mesh(single_tri( 0.5f, gfx::colors::red),   I, r3d::Mode::SolidFlat, light);
    r3.draw_mesh(single_tri(-0.5f, gfx::colors::green), I, r3d::Mode::SolidFlat, light);
    CHECK(gfx::g_of(center()) > gfx::r_of(center()));

    // ...and near drawn FIRST: the far red fragment must be depth-rejected.
    r3.begin(r2, gfx::colors::black);
    r3.draw_mesh(single_tri(-0.5f, gfx::colors::green), I, r3d::Mode::SolidFlat, light);
    r3.draw_mesh(single_tri( 0.5f, gfx::colors::red),   I, r3d::Mode::SolidFlat, light);
    CHECK(gfx::g_of(center()) > gfx::r_of(center()));
}

// ---- cameras ----
static void test_cameras() {
    // Orbit: yaw=0,pitch=0 sits on +Z; the target lands at view-space (0,0,-dist).
    cam::OrbitCamera o;
    o.yaw = 0; o.pitch = 0; o.distance = 5.0f; o.target = {0, 0, 0};
    const math::vec3 e = o.eye();
    CHECK(approx(e.x, 0.0f) && approx(e.y, 0.0f) && approx(e.z, 5.0f));
    const math::vec3 tv = math::transform_point(o.view(), o.target);
    CHECK(approx(tv.x, 0.0f, 1e-4f) && approx(tv.y, 0.0f, 1e-4f) && approx(tv.z, -5.0f, 1e-4f));

    // Orbit 90° in yaw -> camera swings onto +X.
    o.orbit(math::radians(90.0f), 0.0f);
    const math::vec3 e2 = o.eye();
    CHECK(approx(e2.x, 5.0f, 1e-4f) && approx(e2.z, 0.0f, 1e-4f));

    // Zoom + pitch clamp.
    o.zoom(0.5f);
    CHECK(approx(o.distance, 2.5f));
    o.orbit(0.0f, math::radians(1000.0f));
    CHECK(o.pitch < math::radians(90.0f));

    // Fly: yaw=0 looks down -Z; moving forward decreases z.
    cam::FlyCamera f;
    f.pos = {0, 0, 0}; f.yaw = 0; f.pitch = 0;
    const math::vec3 fwd = f.forward();
    CHECK(approx(fwd.x, 0.0f) && approx(fwd.y, 0.0f) && approx(fwd.z, -1.0f));
    f.move(2.0f, 0.0f, 0.0f);
    CHECK(approx(f.pos.z, -2.0f, 1e-4f));
    f.look(0.0f, math::radians(1000.0f));
    CHECK(f.pitch < math::radians(90.0f));
}

// ---- regression: a triangle straddling the near plane, drawn through a real
//      perspective camera, must clip + raster safely (no UB, bounded writes)
//      and still produce visible pixels. Exercises the Gouraud color clamp and
//      the bounding-box clamp on extreme screen coordinates.
static void test_near_clip_raster() {
    const int W = 80, H = 80;
    std::vector<uint32_t> buf(static_cast<size_t>(W * H));
    platform::Framebuffer fb{buf.data(), W, H, W};
    gfx::Renderer2D r2(fb);
    r3d::Renderer3D r3;
    r3.set_cull(false);
    r3.set_camera(math::mat4_look_at({0, 0, 0}, {0, 0, -1}, {0, 1, 0}),
                  math::mat4_perspective(math::radians(70.0f), 1.0f, 0.1f, 100.0f));
    const r3d::Light light;

    geo::Mesh m;
    m.vertices = {
        {{0.0f, 0.0f,  1.0f}, {0, 0, 1}, gfx::colors::white},  // BEHIND the camera
        {{-1.0f, -1.0f, -2.0f}, {0, 0, 1}, gfx::colors::white},
        {{ 1.0f, -1.0f, -2.0f}, {0, 0, 1}, gfx::colors::white},
    };
    m.indices = {0, 1, 2};

    r3.begin(r2, gfx::colors::black);
    r3.draw_mesh(m, math::mat4_identity(), r3d::Mode::SolidGouraud, light);  // reaching here = no crash

    int painted = 0;
    for (uint32_t px : buf) if (px != gfx::colors::black) ++painted;
    CHECK(painted > 0);  // the front portion clipped to the near plane still renders
}

int main() {
    test_to_screen();
    test_mvp_projection();
    test_signed_area_winding();
    test_barycentric();
    test_clip_near();
    test_clip_segment();
    test_lerp_color();
    test_geometry();
    test_rasterizer_depth();
    test_cameras();
    test_near_clip_raster();

    if (g_failures == 0) std::printf("render3d: all tests passed\n");
    else                 std::printf("render3d: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
