// =============================================================================
//  engine/renderer3d.cpp  —  the software rasterizer
// =============================================================================
#include "engine/renderer3d.hpp"

#include <algorithm>
#include <cmath>

namespace r3d {

namespace {
constexpr float kFarDepth = 1e30f;  // "empty" depth: nothing is this far

// Multiply a color by a brightness factor, clamped per channel to [0,255].
gfx::Color shade(gfx::Color c, float s) {
    auto ch = [&](uint8_t v) -> uint8_t {
        const float r = static_cast<float>(v) * s;
        return static_cast<uint8_t>(r < 0.0f ? 0.0f : (r > 255.0f ? 255.0f : r));
    };
    return gfx::rgb(ch(gfx::r_of(c)), ch(gfx::g_of(c)), ch(gfx::b_of(c)));
}

// Lambert term for a (unit) normal under a directional light.
float lambert(math::vec3 n, const Light& light) {
    const float d = math::dot(n, -light.dir);     // facing the light source
    const float diffuse = d > 0.0f ? d : 0.0f;
    return light.ambient + (1.0f - light.ambient) * diffuse;
}
} // namespace

void Renderer3D::begin(gfx::Renderer2D& fb, gfx::Color clear) {
    fb_ = &fb;
    w_ = fb.width();
    h_ = fb.height();
    fb.clear(clear);
    // (Re)size and reset the depth buffer. The framebuffer size is fixed for the
    // window, so this allocates once and only refills afterwards.
    const size_t n = static_cast<size_t>(w_) * static_cast<size_t>(h_);
    if (depth_.size() != n) depth_.assign(n, kFarDepth);
    else std::fill(depth_.begin(), depth_.end(), kFarDepth);
}

void Renderer3D::set_camera(const math::mat4& view, const math::mat4& proj) {
    view_ = view;
    proj_ = proj;
}

void Renderer3D::raster_triangle(const ClipV v[3], bool gouraud) {
    const Screen s0 = to_screen(v[0].clip, w_, h_);
    const Screen s1 = to_screen(v[1].clip, w_, h_);
    const Screen s2 = to_screen(v[2].clip, w_, h_);

    // Backface culling via the signed area of the screen-space triangle. After
    // our y-flip, front faces (CCW in the world) have a NEGATIVE signed area.
    const float area = signed_area({s0.x, s0.y}, {s1.x, s1.y}, {s2.x, s2.y});
    if (area == 0.0f) return;             // degenerate: no pixels
    if (cull_ && area > 0.0f) return;     // back face

    // Screen-space bounding box, clamped to the framebuffer.
    int minx = static_cast<int>(std::floor(std::min({s0.x, s1.x, s2.x})));
    int maxx = static_cast<int>(std::ceil (std::max({s0.x, s1.x, s2.x})));
    int miny = static_cast<int>(std::floor(std::min({s0.y, s1.y, s2.y})));
    int maxy = static_cast<int>(std::ceil (std::max({s0.y, s1.y, s2.y})));
    minx = std::max(minx, 0);  maxx = std::min(maxx, w_ - 1);
    miny = std::max(miny, 0);  maxy = std::min(maxy, h_ - 1);

    // Pre-unpack vertex colors as floats for perspective-correct interpolation.
    const float r0 = gfx::r_of(v[0].color), g0 = gfx::g_of(v[0].color), b0 = gfx::b_of(v[0].color);
    const float r1 = gfx::r_of(v[1].color), g1 = gfx::g_of(v[1].color), b1 = gfx::b_of(v[1].color);
    const float r2 = gfx::r_of(v[2].color), g2 = gfx::g_of(v[2].color), b2 = gfx::b_of(v[2].color);

    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            const math::vec2 p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            const math::vec3 bc = barycentric({s0.x, s0.y}, {s1.x, s1.y}, {s2.x, s2.y}, p);
            // Inside test (tiny epsilon closes seams between adjacent triangles).
            if (bc.x < -1e-5f || bc.y < -1e-5f || bc.z < -1e-5f) continue;

            // Depth is LINEAR in screen space (this is the correct hyperbolic
            // depth), so a plain barycentric blend is right for the z-buffer.
            const float depth = bc.x * s0.depth + bc.y * s1.depth + bc.z * s2.depth;
            const size_t di = static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x);
            if (depth >= depth_[di]) continue;  // something nearer already here
            depth_[di] = depth;

            gfx::Color c;
            if (gouraud) {
                // Perspective-correct: weight by bary*(1/w), then divide. (Flat
                // shading skips this — all three colors are already equal.)
                const float w0 = bc.x * s0.inv_w, w1 = bc.y * s1.inv_w, w2 = bc.z * s2.inv_w;
                const float ws = w0 + w1 + w2;
                if (ws == 0.0f) continue;
                const float inv = 1.0f / ws;
                c = gfx::rgb(static_cast<uint8_t>((w0 * r0 + w1 * r1 + w2 * r2) * inv),
                             static_cast<uint8_t>((w0 * g0 + w1 * g1 + w2 * g2) * inv),
                             static_cast<uint8_t>((w0 * b0 + w1 * b1 + w2 * b2) * inv));
            } else {
                c = v[0].color;  // flat: constant face color
            }
            fb_->set_pixel(x, y, c);
        }
    }
}

void Renderer3D::draw_mesh(const geo::Mesh& mesh, const math::mat4& model, Mode mode,
                           const Light& light) {
    const math::mat4 mvp = proj_ * view_ * model;
    const bool gouraud = (mode == Mode::SolidGouraud);

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const geo::Vertex& a = mesh.vertices[mesh.indices[i]];
        const geo::Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const geo::Vertex& c = mesh.vertices[mesh.indices[i + 2]];

        // Per-vertex colors fed to the rasterizer depend on the shading mode.
        gfx::Color ca, cb, cc;
        if (mode == Mode::SolidFlat) {
            // One Lambert color from the FACE normal (world-space cross product).
            const math::vec3 wa = math::transform_point(model, a.pos);
            const math::vec3 wb = math::transform_point(model, b.pos);
            const math::vec3 wc = math::transform_point(model, c.pos);
            const math::vec3 fn = math::normalize(math::cross(wb - wa, wc - wa));
            const gfx::Color fc = shade(a.color, lambert(fn, light));
            ca = cb = cc = fc;
        } else if (mode == Mode::SolidGouraud) {
            // Lambert per vertex from its (model-rotated) normal.
            ca = shade(a.color, lambert(math::normalize(math::transform_dir(model, a.normal)), light));
            cb = shade(b.color, lambert(math::normalize(math::transform_dir(model, b.normal)), light));
            cc = shade(c.color, lambert(math::normalize(math::transform_dir(model, c.normal)), light));
        } else {
            ca = a.color; cb = b.color; cc = c.color;  // wireframe: raw colors
        }

        ClipV tri[3] = {
            {mvp * math::vec4{a.pos.x, a.pos.y, a.pos.z, 1.0f}, ca},
            {mvp * math::vec4{b.pos.x, b.pos.y, b.pos.z, 1.0f}, cb},
            {mvp * math::vec4{c.pos.x, c.pos.y, c.pos.z, 1.0f}, cc},
        };

        // Near-plane clip first (so no vertex has w <= 0 at the divide).
        ClipV out[2][3];
        const int ntris = clip_near(tri, out);
        for (int t = 0; t < ntris; ++t) {
            if (mode == Mode::Wireframe) {
                // Draw the three edges of the clipped triangle.
                const Screen p0 = to_screen(out[t][0].clip, w_, h_);
                const Screen p1 = to_screen(out[t][1].clip, w_, h_);
                const Screen p2 = to_screen(out[t][2].clip, w_, h_);
                if (cull_) {
                    const float ar = signed_area({p0.x, p0.y}, {p1.x, p1.y}, {p2.x, p2.y});
                    if (ar >= 0.0f) continue;  // skip back faces (and degenerate)
                }
                const gfx::Color wc2 = out[t][0].color;
                fb_->draw_line(int(p0.x), int(p0.y), int(p1.x), int(p1.y), wc2);
                fb_->draw_line(int(p1.x), int(p1.y), int(p2.x), int(p2.y), wc2);
                fb_->draw_line(int(p2.x), int(p2.y), int(p0.x), int(p0.y), wc2);
            } else {
                raster_triangle(out[t], gouraud);
            }
        }
    }
}

void Renderer3D::draw_lines(const geo::Mesh& mesh, const math::mat4& model) {
    const math::mat4 mvp = proj_ * view_ * model;
    for (size_t i = 0; i + 1 < mesh.indices.size(); i += 2) {
        const geo::Vertex& va = mesh.vertices[mesh.indices[i]];
        const geo::Vertex& vb = mesh.vertices[mesh.indices[i + 1]];
        math::vec4 ca = mvp * math::vec4{va.pos.x, va.pos.y, va.pos.z, 1.0f};
        math::vec4 cb = mvp * math::vec4{vb.pos.x, vb.pos.y, vb.pos.z, 1.0f};

        // Clip the segment to the near plane (w + z >= 0) so behind-camera
        // endpoints don't wrap across the screen.
        float da = ca.w + ca.z, db = cb.w + cb.z;
        if (da < 0.0f && db < 0.0f) continue;          // both behind: skip
        if (da < 0.0f) { const float t = da / (da - db); ca = ca + (cb - ca) * t; }
        else if (db < 0.0f) { const float t = db / (db - da); cb = cb + (ca - cb) * t; }

        const Screen sa = to_screen(ca, w_, h_);
        const Screen sb = to_screen(cb, w_, h_);
        fb_->draw_line(int(sa.x), int(sa.y), int(sb.x), int(sb.y), va.color);
    }
}

} // namespace r3d
