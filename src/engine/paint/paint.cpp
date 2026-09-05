// =============================================================================
//  engine/paint/paint.cpp  —  see paint.hpp
// =============================================================================
#include "engine/paint/paint.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace paint {
namespace {

bool in_bounds(const gfx::Image& img, int x, int y) {
    return x >= 0 && y >= 0 && x < img.w && y < img.h;
}

gfx::Color get(const gfx::Image& img, int x, int y) {
    return img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.w) +
                      static_cast<std::size_t>(x)];
}

void set(gfx::Image& img, int x, int y, gfx::Color c) {
    img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.w) +
               static_cast<std::size_t>(x)] = c;
}

void write(gfx::Image& img, const std::vector<PixelEdit>& px, bool forward) {
    for (const PixelEdit& p : px) {
        if (!in_bounds(img, p.x, p.y)) continue;   // the image was resized under us
        set(img, p.x, p.y, forward ? p.after : p.before);
    }
}

} // namespace

std::vector<PixelEdit> rect_pixels(const gfx::Image& img, int x0, int y0, int x1, int y1,
                                   gfx::Color c) {
    std::vector<PixelEdit> out;
    if (img.w <= 0 || img.h <= 0) return out;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, img.w - 1);
    y1 = std::min(y1, img.h - 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) out.push_back(PixelEdit{x, y, get(img, x, y), c});
    return out;
}

std::vector<PixelEdit> flood_pixels(const gfx::Image& img, int x, int y, gfx::Color c) {
    std::vector<PixelEdit> out;
    if (!in_bounds(img, x, y)) return out;
    const gfx::Color target = get(img, x, y);
    if (target == c) return out;   // the whole region is already this colour

    // Explicit stack, not recursion: a fill over a 512x512 sheet is a quarter of a
    // million frames deep, which is a stack overflow rather than a slow fill.
    std::vector<char> seen(static_cast<std::size_t>(img.w) * static_cast<std::size_t>(img.h), 0);
    std::vector<std::pair<int, int>> todo{{x, y}};
    seen[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.w) +
         static_cast<std::size_t>(x)] = 1;
    while (!todo.empty()) {
        const auto [cx, cy] = todo.back();
        todo.pop_back();
        if (get(img, cx, cy) != target) continue;
        out.push_back(PixelEdit{cx, cy, target, c});
        const int dx[4] = {1, -1, 0, 0};
        const int dy[4] = {0, 0, 1, -1};
        for (int i = 0; i < 4; ++i) {
            const int nx = cx + dx[i], ny = cy + dy[i];
            if (!in_bounds(img, nx, ny)) continue;
            const std::size_t k = static_cast<std::size_t>(ny) * static_cast<std::size_t>(img.w) +
                                  static_cast<std::size_t>(nx);
            if (seen[k]) continue;
            seen[k] = 1;
            todo.push_back({nx, ny});
        }
    }
    return out;
}

void drop_noops(std::vector<PixelEdit>& px) {
    px.erase(std::remove_if(px.begin(), px.end(),
                            [](const PixelEdit& p) { return p.before == p.after; }),
             px.end());
}

std::optional<doc::Command> make_command(gfx::Image& img, std::vector<PixelEdit> px,
                                         std::string label) {
    drop_noops(px);
    if (px.empty()) return std::nullopt;
    gfx::Image* image = &img;
    return doc::Command{
        std::move(label),
        [image, px] { write(*image, px, /*forward*/ true); },
        [image, px] { write(*image, px, /*forward*/ false); },
        /*merge_key*/ 0};
}

void Stroke::begin(gfx::Image& img, gfx::Color c, std::string label) {
    active_ = true;
    img_    = &img;
    colour_ = c;
    label_  = std::move(label);
    px_.clear();
}

void Stroke::touch(int x, int y) {
    if (!active_ || img_ == nullptr) return;
    gfx::Image& img = *img_;
    if (!in_bounds(img, x, y)) return;
    const gfx::Color before = get(img, x, y);
    // This also rejects a pixel the stroke already painted: it now HOLDS colour_, so
    // the check catches the drag coming back over itself. No visited set needed.
    if (before == colour_) return;
    px_.push_back(PixelEdit{x, y, before, colour_});
    set(img, x, y, colour_);
}

void Stroke::touch_line(int x0, int y0, int x1, int y1) {
    // Bresenham, integer only. The alternative — stepping a float and rounding — puts
    // the diagonal half a pixel off at some slopes, and in a pixel editor half a pixel
    // is the whole unit of work.
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        touch(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

std::optional<doc::Command> Stroke::finish() {
    active_ = false;
    if (px_.empty() || img_ == nullptr) return std::nullopt;
    // The image is ALREADY at the after-state (touch wrote through), so this command's
    // apply is a no-op the first time and the real thing on redo. It is idempotent by
    // construction, because it writes absolute values rather than a delta.
    std::vector<PixelEdit> px;
    px.swap(px_);
    return make_command(*img_, std::move(px), label_);
}

} // namespace paint
