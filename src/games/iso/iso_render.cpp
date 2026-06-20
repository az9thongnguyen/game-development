// =============================================================================
//  games/iso/iso_render.cpp  —  isometric drawing, all by hand into Renderer2D
// =============================================================================
#include "games/iso/iso_render.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace iso {
namespace {

using gfx::Color;

// Multiply a color's RGB by a factor (for cheap directional "lighting" on box
// faces): top brightest, sides progressively darker.
Color shade(Color c, float f) {
    auto cl = [&](uint8_t v) {
        const int r = static_cast<int>(static_cast<float>(v) * f);
        return static_cast<uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r));
    };
    return gfx::rgb(cl(gfx::r_of(c)), cl(gfx::g_of(c)), cl(gfx::b_of(c)));
}

Color terrain_color(Terrain t) {
    switch (t) {
        case Terrain::Grass: return gfx::rgb(104, 168, 76);
        case Terrain::Soil:  return gfx::rgb(150, 110, 68);
        case Terrain::Water: return gfx::rgb(70, 120, 200);
        case Terrain::Path:  return gfx::rgb(160, 156, 140);
    }
    return gfx::rgb(104, 168, 76);
}

// ---- primitive fills (every pixel by hand) ---------------------------------

// Filled diamond by integer rows: at row dy the half-width tapers linearly. The
// inclusive span slightly overlaps neighbors, which seamlessly tiles the ground.
void fill_diamond(gfx::Renderer2D& g, int cx, int cy, int hw, int hh, Color c) {
    if (hh <= 0) return;
    for (int dy = -hh; dy <= hh; ++dy) {
        const int half = static_cast<int>(static_cast<float>(hw) *
                         (1.0f - static_cast<float>(std::abs(dy)) / static_cast<float>(hh)));
        const int y = cy + dy;
        for (int x = cx - half; x <= cx + half; ++x) g.set_pixel(x, y, c);
    }
}

void fill_diamond_blend(gfx::Renderer2D& g, int cx, int cy, int hw, int hh, Color c) {
    if (hh <= 0) return;
    for (int dy = -hh; dy <= hh; ++dy) {
        const int half = static_cast<int>(static_cast<float>(hw) *
                         (1.0f - static_cast<float>(std::abs(dy)) / static_cast<float>(hh)));
        const int y = cy + dy;
        for (int x = cx - half; x <= cx + half; ++x) g.blend_pixel(x, y, c);
    }
}

void outline_diamond(gfx::Renderer2D& g, int cx, int cy, int hw, int hh, Color c) {
    g.draw_line(cx, cy - hh, cx + hw, cy, c);   // N→E
    g.draw_line(cx + hw, cy, cx, cy + hh, c);   // E→S
    g.draw_line(cx, cy + hh, cx - hw, cy, c);   // S→W
    g.draw_line(cx - hw, cy, cx, cy - hh, c);   // W→N
}

// Even-odd scanline fill of a convex polygon (used for box side faces). Up to a
// handful of vertices; samples each row at its center.
void fill_poly(gfx::Renderer2D& g, const ScreenPt* p, int n, Color c) {
    float ymin = p[0].y, ymax = p[0].y;
    for (int i = 1; i < n; ++i) { ymin = std::min(ymin, p[i].y); ymax = std::max(ymax, p[i].y); }
    const int y0 = static_cast<int>(std::floor(ymin));
    const int y1 = static_cast<int>(std::ceil(ymax));
    for (int y = y0; y <= y1; ++y) {
        const float yc = static_cast<float>(y) + 0.5f;
        float xs[8];
        int   m = 0;
        for (int i = 0; i < n && m < 8; ++i) {
            const ScreenPt a = p[i], b = p[(i + 1) % n];
            if ((a.y <= yc && b.y > yc) || (b.y <= yc && a.y > yc)) {
                xs[m++] = a.x + (yc - a.y) / (b.y - a.y) * (b.x - a.x);
            }
        }
        for (int i = 0; i < m; ++i)
            for (int j = i + 1; j < m; ++j)
                if (xs[j] < xs[i]) std::swap(xs[i], xs[j]);
        for (int i = 0; i + 1 < m; i += 2) {
            const int xa = static_cast<int>(std::floor(xs[i] + 0.5f));
            const int xb = static_cast<int>(std::floor(xs[i + 1] + 0.5f));
            for (int x = xa; x < xb; ++x) g.set_pixel(x, y, c);
        }
    }
}

void fill_circle(gfx::Renderer2D& g, int cx, int cy, int r, Color c) {
    for (int dy = -r; dy <= r; ++dy) {
        const int dx = static_cast<int>(std::sqrt(static_cast<float>(r * r - dy * dy)));
        for (int x = cx - dx; x <= cx + dx; ++x) g.set_pixel(x, cy + dy, c);
    }
}

// An isometric cuboid rising `height` px from the tile (cx,cy), footprint (hw,hh).
// Side faces first (darker), top diamond last (brightest) so it reads as volume.
void draw_iso_box(gfx::Renderer2D& g, int cx, int cy, int hw, int hh, int height, Color base) {
    const float H = static_cast<float>(height);
    const ScreenPt S{static_cast<float>(cx), static_cast<float>(cy + hh)};
    const ScreenPt E{static_cast<float>(cx + hw), static_cast<float>(cy)};
    const ScreenPt W{static_cast<float>(cx - hw), static_cast<float>(cy)};
    const ScreenPt Sp{S.x, S.y - H}, Ep{E.x, E.y - H}, Wp{W.x, W.y - H};

    const ScreenPt left[4]  = {W, S, Sp, Wp};
    const ScreenPt right[4] = {S, E, Ep, Sp};
    fill_poly(g, left, 4, shade(base, 0.70f));    // down-left wall
    fill_poly(g, right, 4, shade(base, 0.50f));   // down-right wall
    fill_diamond(g, cx, cy - height, hw, hh, shade(base, 1.0f));  // top face
    outline_diamond(g, cx, cy - height, hw, hh, shade(base, 1.15f));
}

// ---- per-object drawing -----------------------------------------------------
void draw_object(gfx::Renderer2D& g, int cx, int cy, ObjKind kind) {
    switch (kind) {
        case ObjKind::Tree: {
            g.fill_rect(cx - 3, cy - 14, 6, 16, gfx::rgb(110, 78, 50));    // trunk
            fill_circle(g, cx, cy - 22, 13, gfx::rgb(46, 130, 58));        // canopy
            fill_circle(g, cx - 5, cy - 18, 8, gfx::rgb(58, 150, 70));     // highlight
            break;
        }
        case ObjKind::Rock:
            draw_iso_box(g, cx, cy, 20, 10, 11, gfx::rgb(140, 140, 150));
            break;
        case ObjKind::House: {
            draw_iso_box(g, cx, cy, 26, 13, 30, gfx::rgb(196, 170, 130));  // walls
            fill_diamond(g, cx, cy - 36, 30, 15, gfx::rgb(168, 70, 56));   // roof
            outline_diamond(g, cx, cy - 36, 30, 15, gfx::rgb(120, 50, 40));
            break;
        }
        case ObjKind::Fence:
            draw_iso_box(g, cx, cy, 28, 14, 12, gfx::rgb(150, 110, 64));
            break;
        case ObjKind::Wheat: {
            const Color gold = gfx::rgb(224, 192, 84), stem = gfx::rgb(150, 168, 70);
            const int off[6][2] = {{-10, 3}, {-4, 5}, {2, 4}, {8, 2}, {-6, -1}, {5, -3}};
            for (const auto& o : off) {
                const int x = cx + o[0], y = cy + o[1];
                g.draw_line(x, y, x, y - 11, stem);
                g.set_pixel(x, y - 12, gold);
                g.set_pixel(x - 1, y - 10, gold);
                g.set_pixel(x + 1, y - 10, gold);
            }
            break;
        }
        case ObjKind::Farmer: {
            fill_diamond_blend(g, cx, cy, 12, 6, gfx::rgba(0, 0, 0, 90));  // shadow
            g.fill_rect(cx - 5, cy - 18, 10, 16, gfx::rgb(60, 96, 200));   // body
            fill_circle(g, cx, cy - 22, 5, gfx::rgb(236, 200, 162));       // head
            fill_diamond(g, cx, cy - 27, 9, 4, gfx::rgb(216, 188, 96));    // straw hat
            break;
        }
    }
}

} // namespace

void render_farm(gfx::Renderer2D& g, const Farm& f, const Camera2D& cam, Vec2i hovered) {
    g.clear(gfx::rgb(38, 44, 60));

    // 1) Ground tiles, back (gx+gy small) to front (large). The outer/inner loop
    //    order naturally yields increasing depth key, so no sort is needed here.
    for (int gy = 0; gy < f.height(); ++gy) {
        for (int gx = 0; gx < f.width(); ++gx) {
            const ScreenPt s = grid_to_screen(static_cast<float>(gx), static_cast<float>(gy), cam.ox, cam.oy);
            const int cx = static_cast<int>(s.x), cy = static_cast<int>(s.y);
            const Color base = terrain_color(f.terrain_at(gx, gy));
            fill_diamond(g, cx, cy, kTileW / 2, kTileH / 2, base);
            outline_diamond(g, cx, cy, kTileW / 2, kTileH / 2, shade(base, 0.84f));
        }
    }

    // 2) Hover highlight.
    if (f.map().in_bounds(hovered.x, hovered.y)) {
        const ScreenPt s = grid_to_screen(static_cast<float>(hovered.x), static_cast<float>(hovered.y), cam.ox, cam.oy);
        const int cx = static_cast<int>(s.x), cy = static_cast<int>(s.y);
        fill_diamond_blend(g, cx, cy, kTileW / 2, kTileH / 2, gfx::rgba(255, 255, 255, 60));
        outline_diamond(g, cx, cy, kTileW / 2, kTileH / 2, gfx::rgb(255, 240, 120));
    }

    // 3) Objects + farmer, depth-sorted by iso key (painter's algorithm).
    struct Drawable { float key; float gx, gy; ObjKind kind; };
    std::vector<Drawable> ds;
    const World& w = f.world();
    ds.reserve(w.alive().size());
    for (const Entity e : w.alive()) {
        const Position*   p = w.positions.get(e);
        const Renderable* r = w.renderables.get(e);
        if (!p || !r) continue;
        ds.push_back({depth_key(p->x, p->y), p->x, p->y, r->kind});
    }
    std::sort(ds.begin(), ds.end(), [](const Drawable& a, const Drawable& b) {
        if (a.key != b.key) return a.key < b.key;
        return a.gy < b.gy;   // stable-ish tie-break
    });
    for (const Drawable& d : ds) {
        const ScreenPt s = grid_to_screen(d.gx, d.gy, cam.ox, cam.oy);
        draw_object(g, static_cast<int>(s.x), static_cast<int>(s.y), d.kind);
    }
}

} // namespace iso
