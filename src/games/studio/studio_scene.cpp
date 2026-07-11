// =============================================================================
//  games/studio/studio_scene.cpp
// =============================================================================
#include "games/studio/studio_scene.hpp"

#include <cstdio>

#include "engine/assets.hpp"
#include "engine/color.hpp"
#include "engine/image.hpp"
#include "engine/ui/theme.hpp"
#include "games/studio/recipe.hpp"

namespace studio {

using platform::MouseButton;

namespace {
struct Ramp { const char* name; gfx::Color lo, hi; };
const Ramp kRamps[] = {
    {"sand",  gfx::rgb(120, 96, 60),  gfx::rgb(224, 200, 150)},
    {"stone", gfx::rgb(40, 42, 48),   gfx::rgb(190, 195, 205)},
    {"grass", gfx::rgb(20, 60, 24),   gfx::rgb(120, 190, 90)},
    {"lava",  gfx::rgb(40, 8, 4),     gfx::rgb(250, 180, 60)},
};
constexpr int kRampCount = int(sizeof(kRamps) / sizeof(kRamps[0]));
const char* kBaseNames[] = {"FBM", "Value", "Perlin", "Checker", "Wood"};
const char* kOpNames[]   = {"None", "Threshold", "Contrast"};
} // namespace

StudioScene::StudioScene() { regenerate(); }

void StudioScene::sync_params() {
    params_.frequency  = freq_f_ < 1 ? 1 : int(freq_f_ + 0.5f);
    params_.octaves    = oct_f_  < 1 ? 1 : int(oct_f_ + 0.5f);
    params_.gain       = gain_f_;
    params_.lacunarity = lac_f_;
    params_.op_amount  = opamt_f_;
    params_.seed       = seed_;
    params_.base       = TextureParams::Base(base_idx_);
    params_.op         = TextureParams::Op(op_idx_);
    params_.lo         = kRamps[ramp_idx_].lo;
    params_.hi         = kRamps[ramp_idx_].hi;
}

void StudioScene::regenerate() { sync_params(); preview_ = generate(params_); }

void StudioScene::save_current() {
    char name[32];
    std::snprintf(name, sizeof(name), "studio_%02d", save_counter_++);
    const std::vector<uint8_t> hrt = gfx::encode_hrt(preview_);
    assets::write_file(std::string("textures/") + name + ".hrt", hrt);
    const std::string rec = to_recipe(params_);
    assets::write_file(std::string("textures/") + name + ".recipe",
                       std::vector<uint8_t>(rec.begin(), rec.end()));
    collection_.push_back(name);
}

void StudioScene::load_saved(const std::string& name) {
    auto bytes = assets::load_file(std::string("textures/") + name + ".recipe");
    if (!bytes) return;
    params_  = from_recipe(std::string(bytes->begin(), bytes->end()));
    freq_f_  = float(params_.frequency); oct_f_ = float(params_.octaves);
    gain_f_  = float(params_.gain);      lac_f_ = float(params_.lacunarity);
    opamt_f_ = float(params_.op_amount); seed_  = params_.seed;
    base_idx_ = int(params_.base);       op_idx_ = int(params_.op);
    dirty_ = true;
}

void StudioScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width(); h_ = g.height();
    g.set_font(ctx.font, ui::theme::sz_body);
    g.clear(gfx::rgb(24, 26, 34));

    // ---- live preview (blit the current image; 2x2 to show it tiles) ----
    const int ps = preview_.w;                     // 128
    const int px = 260, py = 60;
    gfx::Sprite spr{preview_.pixels.data(), preview_.w, preview_.h};
    for (int ty = 0; ty < 2; ++ty)
        for (int tx = 0; tx < 2; ++tx)
            g.blit(spr, px + tx * ps, py + ty * ps);
    g.draw_rect(px, py, ps * 2, ps * 2, gfx::rgb(70, 74, 86));
    g.draw_text(px, py - 18, "preview (2x2 tiled)", ui::theme::text_muted);

    // ---- one immediate-mode pass: params panel + collection panel ----
    ui::Input in;
    in.mx = ctx.input.mouse_x; in.my = ctx.input.mouse_y;
    in.down     = ctx.input.down(MouseButton::Left);
    in.pressed  = ctx.input.pressed(MouseButton::Left);
    in.released = ctx.input.released(MouseButton::Left);

    ui_.begin(&g, in);

    ui_.panel(ui::Rect{12, 12, 232, 300}, "TEXTURE LAB");
    char buf[48];
    std::snprintf(buf, sizeof(buf), "base: %s", kBaseNames[base_idx_]);
    if (ui_.button(buf)) { base_idx_ = (base_idx_ + 1) % 5; dirty_ = true; }
    std::snprintf(buf, sizeof(buf), "op: %s", kOpNames[op_idx_]);
    if (ui_.button(buf)) { op_idx_ = (op_idx_ + 1) % 3; dirty_ = true; }
    std::snprintf(buf, sizeof(buf), "ramp: %s", kRamps[ramp_idx_].name);
    if (ui_.button(buf)) { ramp_idx_ = (ramp_idx_ + 1) % kRampCount; dirty_ = true; }
    if (ui_.slider("frequency",  freq_f_, 1, 32))     dirty_ = true;
    if (ui_.slider("octaves",    oct_f_, 1, 8))       dirty_ = true;
    if (ui_.slider("gain",       gain_f_, 0.1f, 0.9f)) dirty_ = true;
    if (ui_.slider("lacunarity", lac_f_, 2, 4))       dirty_ = true;
    if (ui_.slider("op amount",  opamt_f_, 0, 1))     dirty_ = true;
    if (ui_.button("Randomize seed")) { seed_ = seed_ * 1664525u + 1013904223u; dirty_ = true; }
    if (ui_.button("Save", true))     save_current();

    ui_.panel(ui::Rect{w_ - 180, 12, 168, 300}, "COLLECTION");
    for (size_t i = 0; i < collection_.size(); ++i) {
        char lbl[40];
        std::snprintf(lbl, sizeof(lbl), "%s##%zu", collection_[i].c_str(), i);  // unique id
        if (ui_.button(lbl)) load_saved(collection_[i]);
    }

    ui_.end();

    if (dirty_) { regenerate(); dirty_ = false; }

    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(12, h_ - 22,
                "cycle base/op/ramp - drag sliders - Save writes assets/textures/*.hrt - ESC quits",
                ui::theme::text_muted);
}

} // namespace studio
