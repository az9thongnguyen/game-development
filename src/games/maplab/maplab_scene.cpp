// =============================================================================
//  games/maplab/maplab_scene.cpp
// =============================================================================
#include "games/maplab/maplab_scene.hpp"

#include <algorithm>
#include <cstdio>

#include "engine/assets.hpp"
#include "engine/ui/theme.hpp"
#include "games/maplab/edit.hpp"

namespace maplab {

using platform::MouseButton;

namespace {
const char* kMapDir = "maps/";

// Palette: cell id -> (label, editor colour). id>3 falls back to a magenta.
struct PalEntry { const char* label; gfx::Color color; };
const PalEntry kPalette[] = {
    {"Floor",  gfx::rgb(40, 44, 54)},    // 0
    {"Wall",   gfx::rgb(150, 150, 160)}, // 1
    {"Room",   gfx::rgb(90, 140, 210)},  // 2
    {"Pillar", gfx::rgb(220, 150, 70)},  // 3
};
constexpr int kPalCount = int(sizeof(kPalette) / sizeof(kPalette[0]));

gfx::Color cell_color(uint8_t id) {
    return id < kPalCount ? kPalette[id].color : gfx::rgb(220, 60, 200);
}
} // namespace

MaplabScene::MaplabScene() : map_(bordered(24, 16)) {
    scan_collection();
}

void MaplabScene::scan_collection() {
    collection_.clear();
    // ponytail: probe the Lab's own level_NN naming (0..31), like the texture probe.
    for (int i = 0; i < 32; ++i) {
        char name[16]; std::snprintf(name, sizeof(name), "level_%02d", i);
        if (assets::load_file(std::string(kMapDir) + name + ".map"))
            collection_.push_back(name);
    }
    save_counter_ = int(collection_.size());
}

void MaplabScene::save() {
    char name[16]; std::snprintf(name, sizeof(name), "level_%02d", save_counter_++);
    const std::string s = fps::to_text(map_);
    assets::write_file(std::string(kMapDir) + name + ".map",
                       std::vector<uint8_t>(s.begin(), s.end()));
    if (std::find(collection_.begin(), collection_.end(), name) == collection_.end())
        collection_.push_back(name);
}

void MaplabScene::load(const std::string& name) {
    auto bytes = assets::load_file(std::string(kMapDir) + name + ".map");
    if (!bytes) return;
    auto m = fps::from_text(std::string(bytes->begin(), bytes->end()));
    if (m) map_ = *m;
}

bool MaplabScene::cell_at(int mx, int my, int& cx, int& cy) const {
    if (cell_px_ <= 0) return false;
    cx = (mx - origin_x_) / cell_px_;
    cy = (my - origin_y_) / cell_px_;
    return mx >= origin_x_ && my >= origin_y_ && cx >= 0 && cy >= 0 && cx < map_.w && cy < map_.h;
}

void MaplabScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width(); h_ = g.height();
    g.set_font(ctx.font, ui::theme::sz_body);
    g.clear(gfx::rgb(20, 22, 30));

    // ---- fit the grid into the canvas (left gutter reserved for the palette) ----
    const int gutter = 176, pad = 14;
    const int availW = w_ - gutter - pad, availH = h_ - 2 * pad;
    cell_px_  = std::max(2, std::min(availW / map_.w, availH / map_.h));
    origin_x_ = gutter;
    origin_y_ = pad;

    // ---- draw the grid ----
    for (int y = 0; y < map_.h; ++y)
        for (int x = 0; x < map_.w; ++x) {
            const int px = origin_x_ + x * cell_px_, py = origin_y_ + y * cell_px_;
            g.fill_rect(px, py, cell_px_, cell_px_, cell_color(map_.at(x, y)));
            g.draw_rect(px, py, cell_px_, cell_px_, gfx::rgb(28, 30, 38));   // cell grid line
        }
    g.draw_rect(origin_x_, origin_y_, map_.w * cell_px_, map_.h * cell_px_, gfx::rgb(70, 74, 88));

    // hovered-cell highlight
    int hx, hy;
    const bool hover = cell_at(ctx.input.mouse_x, ctx.input.mouse_y, hx, hy);
    if (hover)
        g.draw_rect(origin_x_ + hx * cell_px_, origin_y_ + hy * cell_px_, cell_px_, cell_px_,
                    gfx::rgb(255, 240, 120));

    // ---- UI pass ----
    ui::Input in;
    in.mx = ctx.input.mouse_x; in.my = ctx.input.mouse_y;
    in.down     = ctx.input.down(MouseButton::Left);
    in.pressed  = ctx.input.pressed(MouseButton::Left);
    in.released = ctx.input.released(MouseButton::Left);
    ui_.begin(&g, in);

    ui_.panel(ui::Rect{12, 12, 152, 250}, "MAP LAB");
    for (int i = 0; i < kPalCount; ++i) {
        char lbl[24];
        std::snprintf(lbl, sizeof(lbl), "%s%s", brush_ == i ? "> " : "", kPalette[i].label);
        if (ui_.button(lbl)) brush_ = uint8_t(i);
    }
    if (ui_.button(fill_mode_ ? "Tool: Fill" : "Tool: Paint")) fill_mode_ = !fill_mode_;
    if (ui_.button("New"))  map_ = bordered(24, 16);
    if (ui_.button("Save")) save();

    if (!collection_.empty()) {
        ui_.panel(ui::Rect{12, 272, 152, std::min(28 + 26 * int(collection_.size()), h_ - 284)}, "SAVED");
        for (size_t i = 0; i < collection_.size(); ++i) {
            char lbl[24];
            std::snprintf(lbl, sizeof(lbl), "%s##%zu", collection_[i].c_str(), i);  // unique id
            if (ui_.button(lbl)) load(collection_[i]);
        }
    }
    ui_.end();

    // ---- canvas interaction (only when the UI didn't consume the mouse) ----
    if (!ui_.hovering_ui() && hover) {
        if (fill_mode_) { if (in.pressed) flood_fill(map_, hx, hy, brush_); }
        else if (in.down) set_cell(map_, hx, hy, brush_);   // drag to paint
    }

    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(gutter, h_ - 20,
                "pick a cell type - Paint drags / Fill floods - New clears - Save -> maps/level_NN.map (load in --fps)",
                ui::theme::text_muted);
}

} // namespace maplab
