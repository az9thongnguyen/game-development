// =============================================================================
//  engine/pipeline.hpp  —  the PURE 3D pipeline math (header-only, testable)
// =============================================================================
//  This header holds the parts of the 3D pipeline that are pure functions of
//  their inputs: projecting a clip-space vertex to the screen, the edge function
//  and barycentric weights that drive rasterization, near-plane clipping, and
//  the signed-area winding test used for backface culling.
//
//  Why a separate header? The same reason M2 split `cast_ray` (pure) from the
//  pixel pass: pure functions can be unit-tested with NO window and NO SDL. The
//  framebuffer-touching loop lives in renderer3d.cpp; the math it relies on lives
//  here and is exercised by tests/test_render3d.cpp.
//
//  Conventions (see also engine/math.hpp and the M3 spec):
//    * Clip space comes out of `proj * view * model * v`. A point is in front of
//      the near plane when  w + z >= 0  (because near maps to z/w == -1).
//    * After the perspective divide we are in NDC [-1,+1]^3.
//    * Screen space: x right, y DOWN (matching renderer2d). We flip y here.
//    * Depth is mapped to [0,1] with near = 0, far = 1, and "smaller = nearer".
// =============================================================================
#pragma once

#include "engine/color.hpp"
#include "engine/math.hpp"

namespace r3d {

// A vertex carried through clip space + near-plane clipping. We keep only what
// the rasterizer needs downstream: the clip-space position and the (already-lit,
// for Gouraud) color. Face/world normals are consumed BEFORE this stage in
// draw_mesh, so clipping never has to re-derive them.
struct ClipV {
    math::vec4 clip{};
    gfx::Color color = gfx::colors::white;
};

// The result of projecting one clip-space vertex to the screen.
struct Screen {
    float x = 0.0f;       // pixel x (sub-pixel, +x right)
    float y = 0.0f;       // pixel y (sub-pixel, +y DOWN — already flipped)
    float depth = 0.0f;   // [0,1], near=0 far=1, smaller is nearer
    float inv_w = 0.0f;   // 1/w_clip — the weight for perspective-correct interp
};

// ---- The edge function ------------------------------------------------------
// E(a,b,p) = (b-a) x (p-a)  (z component of the 2D cross product). Its sign tells
// which side of the directed line a->b the point p is on; its magnitude is twice
// the area of triangle (a,b,p). This single primitive powers BOTH rasterization
// (a pixel is inside a triangle when all three edge functions share a sign) and
// the backface test (the signed area of the whole triangle).
inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// Signed area * 2 of a screen-space triangle. Sign == winding order. After our
// y-flip, a front-facing (CCW-in-world) triangle has a NEGATIVE value here, so
// the rasterizer culls triangles with area >= 0 when culling is on.
inline float signed_area(math::vec2 a, math::vec2 b, math::vec2 c) {
    return edge(a.x, a.y, b.x, b.y, c.x, c.y);
}

// Barycentric weights (w0,w1,w2) of p w.r.t. triangle (a,b,c). They sum to 1 and
// are all >= 0 exactly when p is inside the triangle. Returns all-negative on a
// degenerate (zero-area) triangle so callers can skip it.
inline math::vec3 barycentric(math::vec2 a, math::vec2 b, math::vec2 c, math::vec2 p) {
    const float area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
    // Near-zero (not just exactly zero): a sliver triangle would give 1/area ~ 1e30
    // and blow weights up to Inf, poisoning the z-buffer. Treat it as degenerate.
    if (area > -1e-6f && area < 1e-6f) return {-1.0f, -1.0f, -1.0f};
    const float inv = 1.0f / area;
    const float w0 = edge(b.x, b.y, c.x, c.y, p.x, p.y) * inv;  // weight of a
    const float w1 = edge(c.x, c.y, a.x, a.y, p.x, p.y) * inv;  // weight of b
    const float w2 = edge(a.x, a.y, b.x, b.y, p.x, p.y) * inv;  // weight of c
    return {w0, w1, w2};
}

// ---- Project clip space -> screen -------------------------------------------
// Perspective divide (clip -> NDC), then map NDC to the W*H framebuffer with the
// y-axis flipped and depth squashed into [0,1]. Caller must ensure clip.w != 0
// (near-plane clipping guarantees w + z >= 0, but a vertex exactly on the near
// plane can still have small w; the rasterizer guards the degenerate case).
inline Screen to_screen(const math::vec4& clip, int W, int H) {
    Screen s;
    s.inv_w = 1.0f / clip.w;
    const float ndc_x = clip.x * s.inv_w;
    const float ndc_y = clip.y * s.inv_w;
    const float ndc_z = clip.z * s.inv_w;
    s.x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(W);
    s.y = (1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(H);  // flip y
    s.depth = ndc_z * 0.5f + 0.5f;                                  // [0,1]
    return s;
}

// ---- Near-plane clipping ----------------------------------------------------
// Linearly interpolate a clip vertex (Sutherland-Hodgman needs this at the plane
// crossing). Both the homogeneous position and the color blend by t.
inline gfx::Color lerp_color(gfx::Color a, gfx::Color b, float t) {
    auto ch = [&](uint8_t x, uint8_t y) {
        const float v = static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t;
        return static_cast<uint8_t>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v + 0.5f));
    };
    return gfx::rgba(ch(gfx::r_of(a), gfx::r_of(b)),
                     ch(gfx::g_of(a), gfx::g_of(b)),
                     ch(gfx::b_of(a), gfx::b_of(b)),
                     ch(gfx::a_of(a), gfx::a_of(b)));
}

inline ClipV lerp_clipv(const ClipV& a, const ClipV& b, float t) {
    ClipV r;
    r.clip  = a.clip + (b.clip - a.clip) * t;
    r.color = lerp_color(a.color, b.color, t);
    return r;
}

// ---- 2D screen-space segment clipping (Cohen-Sutherland) --------------------
// Clip a line segment to the framebuffer rect [0,w-1] x [0,h-1], updating the
// endpoints in place. Returns false if the segment is entirely outside.
//
// Why: near-plane clipping handles z, but a vertex near the near plane with a
// large lateral offset can project to a screen coordinate in the hundreds of
// thousands. Casting that to int is undefined behavior, and a Bresenham line to
// it would loop hundreds of thousands of times. Clipping first keeps the cast in
// range, bounds the loop, AND preserves the line's slope (unlike a naive clamp).
inline bool clip_segment(float& x0, float& y0, float& x1, float& y1, float w, float h) {
    constexpr int kIn = 0, kL = 1, kR = 2, kTop = 4, kBot = 8;   // +y is down: top = small y
    const float xmin = 0.0f, ymin = 0.0f, xmax = w - 1.0f, ymax = h - 1.0f;
    auto code = [&](float x, float y) {
        int c = kIn;
        if (x < xmin) c |= kL;   else if (x > xmax) c |= kR;
        if (y < ymin) c |= kTop; else if (y > ymax) c |= kBot;
        return c;
    };
    int c0 = code(x0, y0), c1 = code(x1, y1);
    for (int it = 0; it < 8; ++it) {
        if (!(c0 | c1)) return true;    // both endpoints inside
        if (c0 & c1)    return false;   // both share an outside half-plane → out
        const int co = c0 ? c0 : c1;    // pick an endpoint that is outside
        float x = 0.0f, y = 0.0f;
        if      (co & kBot) { x = x0 + (x1 - x0) * (ymax - y0) / (y1 - y0); y = ymax; }
        else if (co & kTop) { x = x0 + (x1 - x0) * (ymin - y0) / (y1 - y0); y = ymin; }
        else if (co & kR)   { y = y0 + (y1 - y0) * (xmax - x0) / (x1 - x0); x = xmax; }
        else                { y = y0 + (y1 - y0) * (xmin - x0) / (x1 - x0); x = xmin; }
        if (co == c0) { x0 = x; y0 = y; c0 = code(x0, y0); }
        else          { x1 = x; y1 = y; c1 = code(x1, y1); }
    }
    return true;  // bounded-iteration safety net
}

// Clip a triangle against the single near plane (w + z >= 0). This is the one
// plane that MUST be clipped: without it, a vertex behind the camera has w <= 0
// and the perspective divide flips/explodes its screen position, smearing the
// triangle across the view. (x/y/far are handled cheaply by the pixel bounding
// box + depth test, so we don't clip them here.)
//
// A triangle clipped by one plane yields a polygon of 3 or 4 vertices, which we
// fan-triangulate into 1 or 2 triangles. Returns the triangle count (0, 1, 2).
inline int clip_near(const ClipV in[3], ClipV out[2][3]) {
    ClipV poly[4];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const ClipV& cur = in[i];
        const ClipV& nxt = in[(i + 1) % 3];
        const float dc = cur.clip.w + cur.clip.z;   // >= 0 means inside
        const float dn = nxt.clip.w + nxt.clip.z;
        const bool in_cur = dc >= 0.0f;
        const bool in_nxt = dn >= 0.0f;
        if (in_cur) poly[n++] = cur;
        if (in_cur != in_nxt) {                      // edge crosses the plane
            const float t = dc / (dc - dn);
            poly[n++] = lerp_clipv(cur, nxt, t);
        }
    }
    if (n < 3) return 0;                             // fully clipped away
    out[0][0] = poly[0]; out[0][1] = poly[1]; out[0][2] = poly[2];
    if (n == 4) {                                    // quad -> two triangles
        out[1][0] = poly[0]; out[1][1] = poly[2]; out[1][2] = poly[3];
        return 2;
    }
    return 1;
}

} // namespace r3d
