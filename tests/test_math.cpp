// =============================================================================
//  tests/test_math.cpp  —  dependency-free unit tests for engine/math.hpp
// =============================================================================
//  No test framework on purpose: a tiny CHECK macro keeps the tooling trivial and
//  the failure output obvious. The program returns the number of failures, so
//  CTest reports pass (0) / fail (non-zero). Run with `ctest --test-dir build`.
// =============================================================================
#include "engine/math.hpp"

#include <cmath>
#include <cstdio>

using namespace math;

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);       \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// Relative+absolute tolerance so we don't trip on float rounding.
static bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(a) + std::fabs(b));
}
static bool approx(vec3 a, vec3 b, float eps = 1e-4f) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) && approx(a.z, b.z, eps);
}

static void test_vectors() {
    vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK(approx(a + b, vec3{5, 7, 9}));
    CHECK(approx(b - a, vec3{3, 3, 3}));
    CHECK(approx(a * 2.0f, vec3{2, 4, 6}));
    CHECK(approx(2.0f * a, vec3{2, 4, 6}));
    CHECK(approx(dot(a, b), 32.0f));                 // 4+10+18
    CHECK(approx(cross(vec3{1,0,0}, vec3{0,1,0}), vec3{0,0,1}));  // x cross y = z
    CHECK(approx(length(vec3{3, 4, 0}), 5.0f));
    CHECK(approx(length(normalize(vec3{0, 0, 7})), 1.0f));
    CHECK(approx(normalize(vec3{0, 0, 0}), vec3{0, 0, 0}));  // zero stays zero
    CHECK(approx(lerp(vec3{0,0,0}, vec3{10,0,0}, 0.5f), vec3{5,0,0}));
}

static void test_identity_and_translate() {
    mat4 I = mat4_identity();
    CHECK(approx(transform_point(I, vec3{7, -3, 2}), vec3{7, -3, 2}));

    mat4 T = mat4_translate(vec3{10, 20, 30});
    CHECK(approx(transform_point(T, vec3{1, 2, 3}), vec3{11, 22, 33}));
    // A direction (w=0) must IGNORE translation.
    CHECK(approx(transform_dir(T, vec3{1, 2, 3}), vec3{1, 2, 3}));
}

static void test_scale_and_rotate() {
    mat4 S = mat4_scale(vec3{2, 3, 4});
    CHECK(approx(transform_point(S, vec3{1, 1, 1}), vec3{2, 3, 4}));

    // 90° rotations send one basis axis onto another.
    CHECK(approx(transform_point(mat4_rotate_z(radians(90)), vec3{1, 0, 0}), vec3{0, 1, 0}));
    CHECK(approx(transform_point(mat4_rotate_x(radians(90)), vec3{0, 1, 0}), vec3{0, 0, 1}));
    CHECK(approx(transform_point(mat4_rotate_y(radians(90)), vec3{0, 0, 1}), vec3{1, 0, 0}));

    // Arbitrary-axis rotation about Z equals mat4_rotate_z.
    vec3 p{1, 0, 0};
    CHECK(approx(transform_point(mat4_rotate(vec3{0,0,1}, radians(90)), p), vec3{0, 1, 0}));
}

static void test_compose_order() {
    // Translate THEN scale vs scale THEN translate must differ, and matrix
    // multiply must apply right-to-left:  (A*B) * v  ==  A * (B * v).
    mat4 T = mat4_translate(vec3{1, 0, 0});
    mat4 S = mat4_scale(vec3{2, 2, 2});
    // (T*S) scales first, then translates: (1,0,0) -> (2,0,0) -> (3,0,0)
    CHECK(approx(transform_point(T * S, vec3{1, 0, 0}), vec3{3, 0, 0}));
    // (S*T) translates first, then scales: (1,0,0) -> (2,0,0) -> (4,0,0)
    CHECK(approx(transform_point(S * T, vec3{1, 0, 0}), vec3{4, 0, 0}));
}

static void test_look_at() {
    // Camera at (0,0,5) looking at origin: the origin should land 5 units down -Z
    // in view space (i.e. directly in front of the camera).
    mat4 V = mat4_look_at(vec3{0, 0, 5}, vec3{0, 0, 0}, vec3{0, 1, 0});
    CHECK(approx(transform_point(V, vec3{0, 0, 0}), vec3{0, 0, -5}));
    // The camera position maps to the view-space origin.
    CHECK(approx(transform_point(V, vec3{0, 0, 5}), vec3{0, 0, 0}));
}

static void test_perspective_depth() {
    mat4 P = mat4_perspective(radians(90), 1.0f, 1.0f, 100.0f);
    // Near plane (z=-1) maps to NDC z=-1; far plane (z=-100) to NDC z=+1.
    CHECK(approx(transform_point(P, vec3{0, 0, -1}).z,  -1.0f));
    CHECK(approx(transform_point(P, vec3{0, 0, -100}).z, 1.0f));
    // With fov=90 & aspect=1, x==|z| sits on the right clip edge (NDC x=+1).
    CHECK(approx(transform_point(P, vec3{1, 0, -1}).x, 1.0f));
}

static void test_ortho_and_viewport() {
    mat4 O = mat4_ortho(-2, 2, -2, 2, -1, 1);
    CHECK(approx(transform_point(O, vec3{2, 2, 0}),  vec3{1, 1, 0}));   // corner -> NDC corner
    CHECK(approx(transform_point(O, vec3{-2, -2, 0}), vec3{-1, -1, 0}));

    mat4 VP = mat4_viewport(0, 0, 800, 600);
    CHECK(approx(transform_point(VP, vec3{0, 0, 0}),  vec3{400, 300, 0.5f})); // center
    CHECK(approx(transform_point(VP, vec3{1, 1, 1}),  vec3{800, 600, 1.0f})); // top-right, far
}

int main() {
    test_vectors();
    test_identity_and_translate();
    test_scale_and_rotate();
    test_compose_order();
    test_look_at();
    test_perspective_depth();
    test_ortho_and_viewport();

    if (g_failures == 0) {
        std::printf("math: all tests passed\n");
    } else {
        std::printf("math: %d FAILURE(S)\n", g_failures);
    }
    return g_failures;
}
