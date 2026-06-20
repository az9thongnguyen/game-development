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
    m.vertices = {
        {{-h, -h, -h}, {}, color},  // 0
        {{ h, -h, -h}, {}, color},  // 1
        {{ h,  h, -h}, {}, color},  // 2
        {{-h,  h, -h}, {}, color},  // 3
        {{-h, -h,  h}, {}, color},  // 4
        {{ h, -h,  h}, {}, color},  // 5
        {{ h,  h,  h}, {}, color},  // 6
        {{-h,  h,  h}, {}, color},  // 7
    };
    // Each face is a quad wound counter-clockwise when viewed from OUTSIDE, so
    // backface culling keeps exactly the faces pointing at the camera.
    auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        m.indices.insert(m.indices.end(), {a, b, c, a, c, d});
    };
    quad(4, 5, 6, 7);  // front  (+z)
    quad(1, 0, 3, 2);  // back   (-z)
    quad(0, 4, 7, 3);  // left   (-x)
    quad(5, 1, 2, 6);  // right  (+x)
    quad(7, 6, 2, 3);  // top    (+y)
    quad(0, 1, 5, 4);  // bottom (-y)
    recompute_normals(m);
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
