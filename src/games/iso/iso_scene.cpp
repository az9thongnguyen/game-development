// =============================================================================
//  games/iso/iso_scene.cpp  —  input → Farm + camera, plus HUD and save/load
// =============================================================================
#include "games/iso/iso_scene.hpp"

#include <algorithm>
#include <cstdio>

#include "engine/assets.hpp"
#include "engine/color.hpp"
#include "engine/ui/theme.hpp"
#include "games/iso/serialize.hpp"

namespace iso {

using platform::Key;
using platform::MouseButton;

namespace {
constexpr const char* kSavePath = "farm_save.txt";

const char* brush_name(Brush b) {
    switch (b) {
        case Brush::Grass:    return "grass";
        case Brush::Soil:     return "soil";
        case Brush::Water:    return "water";
        case Brush::Path:     return "path";
        case Brush::Tree:     return "tree";
        case Brush::Rock:     return "rock";
        case Brush::House:    return "house";
        case Brush::Fence:    return "fence";
        case Brush::Wheat:    return "wheat";
        case Brush::Bulldoze: return "bulldoze";
    }
    return "?";
}
} // namespace

IsoScene::IsoScene() : farm_(16, 16) {
    farm_.reset_default();
}

Vec2i IsoScene::hovered_cell(const platform::InputState& in) const {
    return screen_to_grid(static_cast<float>(in.mouse_x), static_cast<float>(in.mouse_y),
                          cam_.ox, cam_.oy);
}

void IsoScene::flash(const std::string& msg) {
    status_   = msg;
    status_t_ = 2.0;
}

void IsoScene::apply_brush(int gx, int gy) {
    if (!farm_.map().in_bounds(gx, gy)) return;
    switch (brush_) {
        case Brush::Grass: farm_.set_terrain(gx, gy, Terrain::Grass); break;
        case Brush::Soil:  farm_.set_terrain(gx, gy, Terrain::Soil);  break;
        case Brush::Water: farm_.set_terrain(gx, gy, Terrain::Water); break;
        case Brush::Path:  farm_.set_terrain(gx, gy, Terrain::Path);  break;
        case Brush::Tree:  farm_.place_object(gx, gy, ObjKind::Tree);  break;
        case Brush::Rock:  farm_.place_object(gx, gy, ObjKind::Rock);  break;
        case Brush::House: farm_.place_object(gx, gy, ObjKind::House); break;
        case Brush::Fence: farm_.place_object(gx, gy, ObjKind::Fence); break;
        case Brush::Wheat: farm_.place_object(gx, gy, ObjKind::Wheat); break;
        case Brush::Bulldoze: farm_.remove_object(gx, gy); break;
    }
}

void IsoScene::update(double dt, const platform::InputState& in) {
    const float fdt = static_cast<float>(dt);
    if (status_t_ > 0.0) status_t_ -= dt;

    hovered_ = hovered_cell(in);

    // ---- brush selection ----
    if (in.pressed(Key::Num1)) brush_ = Brush::Grass;
    if (in.pressed(Key::Num2)) brush_ = Brush::Soil;
    if (in.pressed(Key::Num3)) brush_ = Brush::Water;
    if (in.pressed(Key::Num4)) brush_ = Brush::Path;
    if (in.pressed(Key::Num5)) brush_ = Brush::Tree;
    if (in.pressed(Key::Num6)) brush_ = Brush::Rock;
    if (in.pressed(Key::Num7)) brush_ = Brush::House;
    if (in.pressed(Key::Num8)) brush_ = Brush::Fence;
    if (in.pressed(Key::Num9)) brush_ = Brush::Wheat;
    if (in.pressed(Key::Num0)) brush_ = Brush::Bulldoze;
    if (in.pressed(Key::Tab)) {
        constexpr int kBrushCount = static_cast<int>(Brush::Bulldoze) + 1;
        brush_ = static_cast<Brush>((static_cast<int>(brush_) + 1) % kBrushCount);
    }

    // ---- paint with the left button held; drag paints, but re-apply only when
    //      the cursor enters a NEW tile so we don't churn entities every frame ----
    if (in.down(MouseButton::Left)) {
        if (in.pressed(MouseButton::Left) || !(hovered_ == last_paint_)) {
            apply_brush(hovered_.x, hovered_.y);
            last_paint_ = hovered_;
        }
    } else {
        last_paint_ = Vec2i{-9999, -9999};
    }

    // ---- right-click commands the farmer ----
    if (in.pressed(MouseButton::Right) && farm_.map().in_bounds(hovered_.x, hovered_.y)) {
        if (!farm_.command_farmer(hovered_.x, hovered_.y)) flash("no path");
    }

    // ---- camera pan (arrows / WASD), continuous ----
    const float pan = 260.0f * fdt;
    if (in.down(Key::Left)  || in.down(Key::A)) cam_.ox += pan;
    if (in.down(Key::Right) || in.down(Key::D)) cam_.ox -= pan;
    if (in.down(Key::Up)    || in.down(Key::W)) cam_.oy += pan;
    if (in.down(Key::Down)  || in.down(Key::S)) cam_.oy -= pan;
    // Clamp the pan so screen_to_grid's float→int floor can never leave int range
    // (UB), no matter how long a pan key is held. Far beyond any usable offset.
    cam_.ox = std::clamp(cam_.ox, -50000.0f, 50000.0f);
    cam_.oy = std::clamp(cam_.oy, -50000.0f, 50000.0f);

    // ---- save / load / reset ----
    if (in.pressed(Key::F5)) {
        flash(assets::write_file(kSavePath, save_farm(farm_)) ? "saved" : "save failed");
    }
    if (in.pressed(Key::F9)) {
        if (auto bytes = assets::load_file(kSavePath))
            flash(load_farm(farm_, *bytes) ? "loaded" : "load failed (corrupt?)");
        else
            flash("no save file");
    }
    if (in.pressed(Key::R)) { farm_.reset_default(); flash("reset"); }

    // ---- advance the simulation (the farmer walks its path) ----
    farm_.update(dt);
}

void IsoScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width();
    h_ = g.height();
    g.set_font(ctx.font, ui::theme::sz_body);   // AA HUD text (8x8 fallback if null)
    if (ctx.dt > 0.0) fps_ = fps_ * 0.92 + (1.0 / ctx.dt) * 0.08;

    // Keep the highlight tracking the mouse even on frames with zero updates.
    hovered_ = hovered_cell(ctx.input);

    render_farm(g, farm_, cam_, hovered_);

    // ---- HUD ----
    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(8, 8,
        "ISO FARM   1-4: terrain   5-9: objects   0: bulldoze   TAB: next   LMB: paint   RMB: walk farmer",
        ui::theme::text_dim);
    g.draw_text(8, 24,
        "arrows/WASD: pan   F5: save   F9: load   R: reset   ESC: quit",
        ui::theme::text_muted);

    char line[160];
    std::snprintf(line, sizeof(line), "brush: %s    entities: %d    tile: (%d,%d)    fps: %d",
                  brush_name(brush_), static_cast<int>(farm_.world().alive().size()),
                  hovered_.x, hovered_.y, static_cast<int>(fps_ + 0.5));
    g.set_font_size(ui::theme::sz_body);
    g.draw_text(8, 40, line, ui::theme::accent_hover);

    if (status_t_ > 0.0 && !status_.empty())
        g.draw_text(8, h_ - 24, status_.c_str(), ui::theme::warn);
}

} // namespace iso
