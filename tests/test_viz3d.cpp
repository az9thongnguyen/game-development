// =============================================================================
//  tests/test_viz3d.cpp  —  picking + editor model + camera ray (CTest, no SDL)
// =============================================================================
#include "engine/camera.hpp"
#include "engine/math.hpp"
#include "engine/pick.hpp"
#include "games/viz3d/editor.hpp"

#include <cmath>
#include <cstdio>

using math::vec3;

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

static void test_ray_sphere() {
    // Ray from (0,0,5) toward -Z hits the unit sphere at the origin at t = 4.
    CHECK(approx(pick::ray_sphere({0, 0, 5}, {0, 0, -1}, {0, 0, 0}, 1.0f), 4.0f));
    // Aimed away (toward +Z): miss.
    CHECK(pick::ray_sphere({0, 0, 5}, {0, 0, 1}, {0, 0, 0}, 1.0f) < 0.0f);
    // Parallel offset: miss.
    CHECK(pick::ray_sphere({5, 0, 5}, {0, 0, -1}, {0, 0, 0}, 1.0f) < 0.0f);
    // Origin inside the sphere: far (exit) hit at t = 1.
    CHECK(approx(pick::ray_sphere({0, 0, 0}, {0, 0, -1}, {0, 0, 0}, 1.0f), 1.0f));
}

static void test_ray_plane() {
    vec3 hit;
    CHECK(pick::ray_plane_y({0, 5, 0}, math::normalize(vec3{0, -1, 0}), 0.0f, hit));
    CHECK(approx(hit.x, 0.0f) && approx(hit.y, 0.0f) && approx(hit.z, 0.0f));
    // A 45° ray downward from (0,2,0): hits y=0 at z = +2 (toward -? depends on dir).
    CHECK(pick::ray_plane_y({0, 2, 0}, math::normalize(vec3{0, -1, 1}), 0.0f, hit));
    CHECK(approx(hit.y, 0.0f) && approx(hit.z, 2.0f, 1e-3f));
    // Parallel ray: no hit.
    CHECK(!pick::ray_plane_y({0, 2, 0}, {1, 0, 0}, 0.0f, hit));
    // Plane behind the ray: no hit.
    CHECK(!pick::ray_plane_y({0, 2, 0}, {0, 1, 0}, 0.0f, hit));
}

static void test_camera_ray() {
    cam::OrbitCamera c;
    c.target = {0, 0, 0}; c.yaw = 0; c.pitch = 0; c.distance = 5.0f;
    const cam::Ray center = c.ray_through(0.0f, 0.0f, 1.5f);   // through screen center
    CHECK(approx(center.origin.z, 5.0f));                       // starts at the eye
    const vec3 fwd = c.forward();                               // points at the target
    CHECK(approx(center.dir.x, fwd.x) && approx(center.dir.y, fwd.y) && approx(center.dir.z, fwd.z));
    CHECK(approx(math::length(center.dir), 1.0f));              // normalized
}

static void test_editor() {
    viz3d::Editor ed;
    CHECK(ed.selected_index() == -1);

    const int a = ed.spawn(viz3d::Shape::Cube, {0, 0, 0}, gfx::colors::white);
    const int b = ed.spawn(viz3d::Shape::Sphere, {5, 0, 0}, gfx::colors::white);
    CHECK(a == 0 && b == 1);
    CHECK(ed.objects().size() == 2);
    CHECK(ed.selected_index() == 1);          // spawn auto-selects the new object

    // Pick straight down -Z at the origin object → selects the cube (index 0).
    CHECK(ed.pick({0, 0, 5}, {0, 0, -1}) == 0);
    // Pick toward the sphere at x=5.
    CHECK(ed.pick({5, 0, 5}, {0, 0, -1}) == 1);
    // A ray into empty space misses everything.
    CHECK(ed.pick({0, 50, 5}, {0, 0, -1}) == -1);

    // Cycle wraps; select out-of-range deselects.
    ed.select(0); ed.cycle(); CHECK(ed.selected_index() == 1);
    ed.cycle(); CHECK(ed.selected_index() == 0);   // wrapped
    ed.select(99); CHECK(ed.selected_index() == -1);

    // Remove the selected one shrinks the list and deselects.
    ed.select(0); ed.remove_selected();
    CHECK(ed.objects().size() == 1);
    CHECK(ed.selected_index() == -1);
    CHECK(ed.selected() == nullptr);
}

int main() {
    test_ray_sphere();
    test_ray_plane();
    test_camera_ray();
    test_editor();
    if (g_failures == 0) std::printf("viz3d: all tests passed\n");
    else                 std::printf("viz3d: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
