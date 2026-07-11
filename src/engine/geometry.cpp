// =============================================================================
//  engine/geometry.cpp  —  primitive mesh generators
// =============================================================================
#include "engine/geometry.hpp"

#include <cmath>

namespace geo {

void recompute_normals(Mesh& mesh) {
    for (auto& v : mesh.vertices) v.normal = math::vec3{0, 0, 0};
    // Each triangle contributes its face normal (length ∝ area) to its 3 corners.
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        const math::vec3 a = mesh.vertices[ia].pos;
        const math::vec3 b = mesh.vertices[ib].pos;
        const math::vec3 c = mesh.vertices[ic].pos;
        const math::vec3 fn = math::cross(b - a, c - a);  // un-normalized = area-weighted
        mesh.vertices[ia].normal = mesh.vertices[ia].normal + fn;
        mesh.vertices[ib].normal = mesh.vertices[ib].normal + fn;
        mesh.vertices[ic].normal = mesh.vertices[ic].normal + fn;
    }
    for (auto& v : mesh.vertices) v.normal = math::normalize(v.normal);
}

Mesh make_cube(float size, gfx::Color color) {
    const float h = size * 0.5f;
    Mesh m;
    // Per-face vertices (4 each → 24 total), each carrying its FLAT face normal.
    // Shared corners would average to diagonal normals and round the cube under
    // Gouraud shading; per-face normals keep every face crisp in flat AND Gouraud.
    // Quads are wound counter-clockwise viewed from OUTSIDE (backface culling keeps
    // the faces pointing at the camera).
    auto face = [&](math::vec3 a, math::vec3 b, math::vec3 c, math::vec3 d, math::vec3 n) {
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({a, n, color});
        m.vertices.push_back({b, n, color});
        m.vertices.push_back({c, n, color});
        m.vertices.push_back({d, n, color});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    face({-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h}, { 0,  0,  1});  // front  (+z)
    face({ h, -h, -h}, {-h, -h, -h}, {-h,  h, -h}, { h,  h, -h}, { 0,  0, -1});  // back   (-z)
    face({-h, -h, -h}, {-h, -h,  h}, {-h,  h,  h}, {-h,  h, -h}, {-1,  0,  0});  // left   (-x)
    face({ h, -h,  h}, { h, -h, -h}, { h,  h, -h}, { h,  h,  h}, { 1,  0,  0});  // right  (+x)
    face({-h,  h,  h}, { h,  h,  h}, { h,  h, -h}, {-h,  h, -h}, { 0,  1,  0});  // top    (+y)
    face({-h, -h, -h}, { h, -h, -h}, { h, -h,  h}, {-h, -h,  h}, { 0, -1,  0});  // bottom (-y)
    return m;
}

Mesh make_plane(float size, int subdiv, gfx::Color color) {
    if (subdiv < 1) subdiv = 1;
    Mesh m;
    const float h = size * 0.5f;
    const float step = size / static_cast<float>(subdiv);
    const int n = subdiv + 1;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const float x = -h + step * static_cast<float>(i);
            const float z = -h + step * static_cast<float>(j);
            m.vertices.push_back({{x, 0.0f, z}, {0, 1, 0}, color});  // normal +Y
        }
    }
    auto at = [n](int i, int j) { return static_cast<uint32_t>(j * n + i); };
    for (int j = 0; j < subdiv; ++j) {
        for (int i = 0; i < subdiv; ++i) {
            const uint32_t a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
            // Wound so the geometric normal is +Y (faces up).
            m.indices.insert(m.indices.end(), {a, d, c, a, c, b});
        }
    }
    return m;
}

Mesh make_sphere(float radius, int stacks, int slices, gfx::Color color) {
    if (stacks < 2) stacks = 2;
    if (slices < 3) slices = 3;
    Mesh m;
    for (int i = 0; i <= stacks; ++i) {
        const float lat = math::kPi * static_cast<float>(i) / static_cast<float>(stacks);  // 0..pi
        const float sl = std::sin(lat), cl = std::cos(lat);
        for (int j = 0; j <= slices; ++j) {
            const float lon = 2.0f * math::kPi * static_cast<float>(j) / static_cast<float>(slices);
            const math::vec3 n{sl * std::cos(lon), cl, sl * std::sin(lon)};
            m.vertices.push_back({n * radius, n, color});  // normal = unit position
        }
    }
    const int row = slices + 1;
    auto at = [row](int i, int j) { return static_cast<uint32_t>(i * row + j); };
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            // Wound CCW from outside (verified against the equator).
            m.indices.insert(m.indices.end(),
                {at(i, j), at(i, j + 1), at(i + 1, j),
                 at(i, j + 1), at(i + 1, j + 1), at(i + 1, j)});
        }
    }
    return m;
}

Mesh make_cylinder(float radius, float height, int slices, gfx::Color color) {
    if (slices < 3) slices = 3;
    Mesh m;
    const float hy = height * 0.5f;

    // --- side wall: paired bottom/top vertices with radial normals ---
    for (int j = 0; j <= slices; ++j) {
        const float a = 2.0f * math::kPi * static_cast<float>(j) / static_cast<float>(slices);
        const math::vec3 n{std::cos(a), 0.0f, std::sin(a)};
        m.vertices.push_back({{n.x * radius, -hy, n.z * radius}, n, color});  // bottom (2j)
        m.vertices.push_back({{n.x * radius,  hy, n.z * radius}, n, color});  // top    (2j+1)
    }
    for (int j = 0; j < slices; ++j) {
        const uint32_t b0 = static_cast<uint32_t>(2 * j), t0 = b0 + 1;
        const uint32_t b1 = static_cast<uint32_t>(2 * (j + 1)), t1 = b1 + 1;
        m.indices.insert(m.indices.end(), {b0, t0, b1, b1, t0, t1});  // CCW from outside
    }

    // --- top cap (normal +Y): a triangle fan around the center ---
    const uint32_t top_base = static_cast<uint32_t>(m.vertices.size());
    for (int j = 0; j <= slices; ++j) {
        const float a = 2.0f * math::kPi * static_cast<float>(j) / static_cast<float>(slices);
        m.vertices.push_back({{std::cos(a) * radius, hy, std::sin(a) * radius}, {0, 1, 0}, color});
    }
    const uint32_t top_center = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({{0, hy, 0}, {0, 1, 0}, color});
    for (int j = 0; j < slices; ++j)
        m.indices.insert(m.indices.end(),
            {top_center, top_base + static_cast<uint32_t>(j + 1), top_base + static_cast<uint32_t>(j)});

    // --- bottom cap (normal -Y) ---
    const uint32_t bot_base = static_cast<uint32_t>(m.vertices.size());
    for (int j = 0; j <= slices; ++j) {
        const float a = 2.0f * math::kPi * static_cast<float>(j) / static_cast<float>(slices);
        m.vertices.push_back({{std::cos(a) * radius, -hy, std::sin(a) * radius}, {0, -1, 0}, color});
    }
    const uint32_t bot_center = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({{0, -hy, 0}, {0, -1, 0}, color});
    for (int j = 0; j < slices; ++j)
        m.indices.insert(m.indices.end(),
            {bot_center, bot_base + static_cast<uint32_t>(j), bot_base + static_cast<uint32_t>(j + 1)});

    return m;
}

Mesh make_grid(float half_extent, int divisions, gfx::Color color) {
    if (divisions < 1) divisions = 1;
    Mesh m;
    const float step = (2.0f * half_extent) / static_cast<float>(divisions);
    for (int k = 0; k <= divisions; ++k) {
        const float t = -half_extent + step * static_cast<float>(k);
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({{-half_extent, 0, t}, {0, 1, 0}, color});  // line along X
        m.vertices.push_back({{ half_extent, 0, t}, {0, 1, 0}, color});
        m.vertices.push_back({{t, 0, -half_extent}, {0, 1, 0}, color});  // line along Z
        m.vertices.push_back({{t, 0,  half_extent}, {0, 1, 0}, color});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base + 3});
    }
    return m;
}

Mesh make_axes(float length) {
    Mesh m;
    m.vertices = {
        {{0, 0, 0}, {}, gfx::colors::red},   {{length, 0, 0}, {}, gfx::colors::red},   // X
        {{0, 0, 0}, {}, gfx::colors::green}, {{0, length, 0}, {}, gfx::colors::green}, // Y
        {{0, 0, 0}, {}, gfx::colors::blue},  {{0, 0, length}, {}, gfx::colors::blue},  // Z
    };
    m.indices = {0, 1, 2, 3, 4, 5};
    return m;
}

} // namespace geo
