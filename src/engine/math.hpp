// =============================================================================
//  engine/math.hpp  —  hand-written linear algebra (vec2/3/4, mat4)
// =============================================================================
//  This is the foundation for ALL geometry: 2D drawing now, and the real 3D
//  transform pipeline at M3 (model → view → projection → viewport). It is
//  header-only and inline so it costs nothing to include anywhere.
//
//  Conventions (read these once; they prevent 90% of matrix bugs):
//    * Angles are in RADIANS. Use radians(deg) to convert.
//    * Matrices are stored COLUMN-MAJOR, matching OpenGL. Element (row r, col c)
//      lives at m[c*4 + r]. Use at(r, c) instead of indexing m[] directly.
//    * Vectors are COLUMN vectors; we transform with  M * v  (matrix on the left).
//    * Right-handed coordinate system; the camera looks down -Z. Projection maps
//      depth into the OpenGL clip range [-1, +1] (near → -1, far → +1).
// =============================================================================
#pragma once

#include <cmath>

namespace math {

// ---- Small scalar helpers ---------------------------------------------------
constexpr float kPi = 3.14159265358979323846f;

inline float radians(float deg) { return deg * (kPi / 180.0f); }
inline float degrees(float rad) { return rad * (180.0f / kPi); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// =============================================================================
//  Vectors
// =============================================================================
struct vec2 {
    float x = 0.0f, y = 0.0f;
};
struct vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
struct vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

// ---- vec2 ----
inline vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline vec2 operator-(vec2 a)         { return {-a.x, -a.y}; }
inline vec2 operator*(vec2 a, float s){ return {a.x * s, a.y * s}; }
inline vec2 operator*(float s, vec2 a){ return {a.x * s, a.y * s}; }
inline vec2 operator/(vec2 a, float s){ return {a.x / s, a.y / s}; }
inline float dot(vec2 a, vec2 b)      { return a.x * b.x + a.y * b.y; }
inline float length2(vec2 a)          { return dot(a, a); }
inline float length(vec2 a)           { return std::sqrt(length2(a)); }
inline vec2 normalize(vec2 a)         { float l = length(a); return l > 0.0f ? a / l : a; }

// ---- vec3 ----
inline vec3 operator+(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline vec3 operator-(vec3 a, vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline vec3 operator-(vec3 a)         { return {-a.x, -a.y, -a.z}; }
inline vec3 operator*(vec3 a, float s){ return {a.x * s, a.y * s, a.z * s}; }
inline vec3 operator*(float s, vec3 a){ return {a.x * s, a.y * s, a.z * s}; }
inline vec3 operator/(vec3 a, float s){ return {a.x / s, a.y / s, a.z / s}; }
inline float dot(vec3 a, vec3 b)      { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) {
    return { a.y * b.z - a.z * b.y,
             a.z * b.x - a.x * b.z,
             a.x * b.y - a.y * b.x };
}
inline float length2(vec3 a) { return dot(a, a); }
inline float length(vec3 a)  { return std::sqrt(length2(a)); }
inline vec3 normalize(vec3 a){ float l = length(a); return l > 0.0f ? a / l : a; }
inline vec3 lerp(vec3 a, vec3 b, float t) { return a + (b - a) * t; }

// ---- vec4 ----
inline vec4 operator+(vec4 a, vec4 b) { return {a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w}; }
inline vec4 operator-(vec4 a, vec4 b) { return {a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w}; }
inline vec4 operator*(vec4 a, float s){ return {a.x*s, a.y*s, a.z*s, a.w*s}; }
inline float dot(vec4 a, vec4 b)      { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }

// =============================================================================
//  mat4  (column-major; element (row r, col c) == m[c*4 + r])
// =============================================================================
struct mat4 {
    float m[16] = {0.0f};

    float&       at(int r, int c)       { return m[c * 4 + r]; }
    const float& at(int r, int c) const { return m[c * 4 + r]; }
};

inline mat4 mat4_identity() {
    mat4 r;
    r.at(0, 0) = 1.0f;
    r.at(1, 1) = 1.0f;
    r.at(2, 2) = 1.0f;
    r.at(3, 3) = 1.0f;
    return r;
}

// Matrix * matrix:  (A*B)(r,c) = sum_k A(r,k) * B(k,c)
inline mat4 operator*(const mat4& a, const mat4& b) {
    mat4 r;  // zero-initialised
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.at(row, k) * b.at(k, c);
            }
            r.at(row, c) = sum;
        }
    }
    return r;
}

// Matrix * column-vector:  result_r = sum_c M(r,c) * v_c
inline vec4 operator*(const mat4& a, vec4 v) {
    return {
        a.at(0,0)*v.x + a.at(0,1)*v.y + a.at(0,2)*v.z + a.at(0,3)*v.w,
        a.at(1,0)*v.x + a.at(1,1)*v.y + a.at(1,2)*v.z + a.at(1,3)*v.w,
        a.at(2,0)*v.x + a.at(2,1)*v.y + a.at(2,2)*v.z + a.at(2,3)*v.w,
        a.at(3,0)*v.x + a.at(3,1)*v.y + a.at(3,2)*v.z + a.at(3,3)*v.w,
    };
}

// Transform a POINT (implicit w=1), with perspective divide if w != 1.
inline vec3 transform_point(const mat4& a, vec3 p) {
    vec4 r = a * vec4{p.x, p.y, p.z, 1.0f};
    if (r.w != 0.0f && r.w != 1.0f) {
        return {r.x / r.w, r.y / r.w, r.z / r.w};
    }
    return {r.x, r.y, r.z};
}

// Transform a DIRECTION (w=0): ignores translation, no divide.
inline vec3 transform_dir(const mat4& a, vec3 d) {
    vec4 r = a * vec4{d.x, d.y, d.z, 0.0f};
    return {r.x, r.y, r.z};
}

// ---- Affine builders --------------------------------------------------------
inline mat4 mat4_translate(vec3 t) {
    mat4 r = mat4_identity();
    r.at(0, 3) = t.x;
    r.at(1, 3) = t.y;
    r.at(2, 3) = t.z;
    return r;
}

inline mat4 mat4_scale(vec3 s) {
    mat4 r;
    r.at(0, 0) = s.x;
    r.at(1, 1) = s.y;
    r.at(2, 2) = s.z;
    r.at(3, 3) = 1.0f;
    return r;
}

inline mat4 mat4_rotate_x(float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    mat4 r = mat4_identity();
    r.at(1, 1) = c;  r.at(1, 2) = -s;
    r.at(2, 1) = s;  r.at(2, 2) =  c;
    return r;
}

inline mat4 mat4_rotate_y(float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    mat4 r = mat4_identity();
    r.at(0, 0) =  c;  r.at(0, 2) = s;
    r.at(2, 0) = -s;  r.at(2, 2) = c;
    return r;
}

inline mat4 mat4_rotate_z(float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    mat4 r = mat4_identity();
    r.at(0, 0) = c;  r.at(0, 1) = -s;
    r.at(1, 0) = s;  r.at(1, 1) =  c;
    return r;
}

// Rotation by `angle` around an arbitrary axis (Rodrigues' formula).
inline mat4 mat4_rotate(vec3 axis, float angle) {
    axis = normalize(axis);
    const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
    const float x = axis.x, y = axis.y, z = axis.z;
    mat4 r = mat4_identity();
    r.at(0,0) = t*x*x + c;    r.at(0,1) = t*x*y - s*z;  r.at(0,2) = t*x*z + s*y;
    r.at(1,0) = t*x*y + s*z;  r.at(1,1) = t*y*y + c;    r.at(1,2) = t*y*z - s*x;
    r.at(2,0) = t*x*z - s*y;  r.at(2,1) = t*y*z + s*x;  r.at(2,2) = t*z*z + c;
    return r;
}

// ---- Camera / projection (right-handed, clip depth [-1, +1]) ----------------
inline mat4 mat4_look_at(vec3 eye, vec3 center, vec3 up) {
    const vec3 f = normalize(center - eye);  // forward (camera looks along +f)
    const vec3 s = normalize(cross(f, up));  // right
    const vec3 u = cross(s, f);              // true up
    mat4 r = mat4_identity();
    r.at(0,0) = s.x;  r.at(0,1) = s.y;  r.at(0,2) = s.z;  r.at(0,3) = -dot(s, eye);
    r.at(1,0) = u.x;  r.at(1,1) = u.y;  r.at(1,2) = u.z;  r.at(1,3) = -dot(u, eye);
    r.at(2,0) = -f.x; r.at(2,1) = -f.y; r.at(2,2) = -f.z; r.at(2,3) =  dot(f, eye);
    return r;
}

inline mat4 mat4_perspective(float fovy_radians, float aspect, float near, float far) {
    const float f = 1.0f / std::tan(fovy_radians * 0.5f);
    mat4 r;  // zero
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (far + near) / (near - far);
    r.at(2, 3) = (2.0f * far * near) / (near - far);
    r.at(3, 2) = -1.0f;
    return r;
}

inline mat4 mat4_ortho(float l, float rt, float b, float t, float n, float f) {
    mat4 r = mat4_identity();
    r.at(0, 0) =  2.0f / (rt - l);
    r.at(1, 1) =  2.0f / (t - b);
    r.at(2, 2) = -2.0f / (f - n);
    r.at(0, 3) = -(rt + l) / (rt - l);
    r.at(1, 3) = -(t + b) / (t - b);
    r.at(2, 3) = -(f + n) / (f - n);
    return r;
}

// Maps normalized device coords [-1,+1] to a window rectangle of size w*h at
// origin (x,y), with depth mapped to [0,1]. (Standard GL viewport; screen-space
// y-flip is handled at rasterization time in M3.)
inline mat4 mat4_viewport(float x, float y, float w, float h) {
    mat4 r = mat4_identity();
    r.at(0, 0) = w * 0.5f;  r.at(0, 3) = x + w * 0.5f;
    r.at(1, 1) = h * 0.5f;  r.at(1, 3) = y + h * 0.5f;
    r.at(2, 2) = 0.5f;      r.at(2, 3) = 0.5f;
    return r;
}

} // namespace math
