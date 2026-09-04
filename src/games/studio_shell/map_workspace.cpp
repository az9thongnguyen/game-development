// =============================================================================
//  games/studio_shell/map_workspace.cpp
// =============================================================================
#include "games/studio_shell/map_workspace.hpp"

#include <algorithm>
#include <utility>

#include "engine/commands/registry.hpp"
#include "engine/document/document.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"

namespace studioshell {

namespace th = ui::theme;

namespace {

// Autosave cadence. Long enough that a stroke does not write a file per frame, short
// enough that a crash costs a sentence rather than an afternoon.
constexpr double kAutosaveSeconds = 10.0;

constexpr const char* kToolNames[] = {"Paint", "Rect", "Fill"};

// Ten stable, distinguishable colours. Deliberately not an evenly spaced hue ramp at
// one lightness — that is hard to separate for a large minority of people, and these
// ids are how the author identifies a tile until there is art.
constexpr gfx::Color kTilePalette[] = {
    0xFF161A24,  // 0 = empty
    0xFF5AAAE6, 0xFF4CC38A, 0xFFE5B454, 0xFFE5657A,
    0xFFC792EA, 0xFF67C7E5, 0xFF9AA7B8, 0xFFB07B4A, 0xFF7FB069,
};
constexpr int kPaletteN = static_cast<int>(sizeof(kTilePalette) / sizeof(kTilePalette[0]));

} // namespace

gfx::Color tile_color(std::int32_t id) {
    if (id <= 0) return kTilePalette[0];
    // Ids beyond the palette wrap rather than clamp: two different tiles sharing a
    // colour is confusing, but every tile above 9 being the same colour is worse.
    return kTilePalette[1 + static_cast<int>((id - 1) % (kPaletteN - 1))];
}

// ---- construction ------------------------------------------------------------

MapWorkspace::MapWorkspace(std::string map_path) : path_(std::move(map_path)) { load(); }

MapWorkspace::~MapWorkspace() {
    if (!commands_registered_) return;
    // Every handler captured `this`. Leaving them registered would leave the palette
    // holding a call into freed memory.
    for (const char* id : {"map.save", "map.undo", "map.redo", "map.reload"})
        cmd::unregister(id);
}

void MapWorkspace::load() {
    loaded_ = false;
    problem_.clear();
    stack_.clear();
    recovery_pending_ = false;
    recovery_text_.clear();

    if (path_.empty()) {
        problem_ = "this project declares no map - add `asset map <path>` to its manifest";
        return;
    }
    const doc::Opened opened = doc::open(path_);
    if (opened.state == doc::OpenState::Missing) {
        problem_ = "cannot read " + path_;
        return;
    }
    auto parsed = tilemap::load(opened.content);
    if (!parsed) {
        problem_ = path_ + " is not a map this build understands";
        return;
    }
    map_    = std::move(*parsed);
    loaded_ = true;
    layer_  = 0;
    centred_ = false;   // recentre on the new map's size, not the old one
    if (opened.state == doc::OpenState::RecoveryOffered) {
        recovery_pending_ = true;
        recovery_text_    = opened.recovered;
    }
}

void MapWorkspace::take_recovery() {
    recovery_pending_ = false;
    auto parsed = tilemap::load(recovery_text_);
    recovery_text_.clear();
    if (!parsed) { note(false, "the autosave could not be parsed; kept the saved file"); return; }

    // Recovery is an EDIT, not a load: it goes on the undo stack like any other, so
    // the document opens dirty (the file on disk still holds the older version) and
    // one Ctrl+Z takes the user back to what they had saved.
    tilemap::Map* target = &map_;
    tilemap::Map  before = map_;
    tilemap::Map  after  = std::move(*parsed);
    stack_.push_apply(doc::Command{"recover unsaved changes",
                                   [target, after] { *target = after; },
                                   [target, before] { *target = before; },
                                   0});
    note(true, "recovered unsaved changes - not yet saved");
}

void MapWorkspace::dismiss_recovery() {
    recovery_pending_ = false;
    recovery_text_.clear();
    note(true, "kept the saved version; the autosave is still on disk");
}

// ---- commands ----------------------------------------------------------------

void MapWorkspace::register_commands() {
    const auto did = [](bool ok, const char* yes, const char* no) {
        return engine::OpResult{ok, ok ? yes : no};
    };
    cmd::register_command(cmd::Info{"map.save", "Map: save", "Cmd+S", ""},
                          [this](const std::vector<std::string>&) { return save(); });
    cmd::register_command(cmd::Info{"map.undo", "Map: undo", "Cmd+Z", ""},
                          [this, did](const std::vector<std::string>&) {
                              return did(stack_.undo(), "undone", "nothing to undo");
                          });
    cmd::register_command(cmd::Info{"map.redo", "Map: redo", "Shift+Cmd+Z", ""},
                          [this, did](const std::vector<std::string>&) {
                              return did(stack_.redo(), "redone", "nothing to redo");
                          });
    cmd::register_command(cmd::Info{"map.reload", "Map: reload from disk", "", ""},
                          [this](const std::vector<std::string>&) { return reload(); });
    commands_registered_ = true;
}

engine::OpResult MapWorkspace::save() {
    if (!loaded_) return engine::OpResult{false, "no map to save"};
    if (!doc::save(path_, tilemap::to_text(map_)))
        return engine::OpResult{false, "could not write " + path_};
    stack_.mark_saved();
    autosave_timer_ = 0.0;
    return engine::OpResult{true, "saved " + path_};
}

engine::OpResult MapWorkspace::reload() {
    load();
    return loaded_ ? engine::OpResult{true, "reloaded " + path_}
                   : engine::OpResult{false, problem_};
}

void MapWorkspace::note(bool ok, std::string msg) {
    message_ = engine::OpResult{ok, std::move(msg)};
}

// The status line, moved off the shell. It used to be assembled there, which meant
// the shell knew this document had tiles — the one fact a second workspace made
// impossible to keep.
std::string MapWorkspace::status() const {
    if (!loaded_) return problem_.empty() ? std::string("no map") : problem_;
    std::string s = path_ + (dirty() ? "  *  unsaved" : "  saved");
    if (hover_x_ >= 0)
        s += "   tile " + std::to_string(hover_x_) + ", " + std::to_string(hover_y_);
    return s;
}

const char* MapWorkspace::hint() const {
    return "Cmd+K commands   Cmd+S save   Cmd+Z undo   MMB pan   wheel zoom";
}

std::optional<engine::OpResult> MapWorkspace::take_message() {
    auto m = message_;
    message_.reset();
    return m;
}

ui::Rect MapWorkspace::tile_rect(int tx, int ty) const {
    const int tile = map_.tile * zoom_;
    if (canvas_.w <= 0 || tile <= 0) return ui::Rect{};
    return ui::Rect{canvas_.x + pan_x_ + tx * tile, canvas_.y + pan_y_ + ty * tile, tile, tile};
}

std::string MapWorkspace::layer_name() const {
    if (layer_ < 0 || layer_ >= static_cast<int>(map_.layers.size())) return {};
    return map_.layers[static_cast<std::size_t>(layer_)].name;
}

// ---- update ------------------------------------------------------------------

void MapWorkspace::update(double dt, const platform::InputState& in, bool interactive) {
    // Inspector clicks resolved during the previous draw, where the layout is known.
    if (want_layer_ >= 0) { layer_ = want_layer_; want_layer_ = -1; }
    if (want_tool_  >= 0) { tool_  = static_cast<Tool>(want_tool_); want_tool_ = -1; }
    if (want_brush_ >= 0) { brush_ = want_brush_; want_brush_ = -1; }
    if (want_undo_) { want_undo_ = false; if (!stack_.undo()) note(false, "nothing to undo"); }
    if (want_redo_) { want_redo_ = false; if (!stack_.redo()) note(false, "nothing to redo"); }
    if (want_save_) { want_save_ = false; message_ = save(); }

    if (!loaded_) return;

    // Autosave runs on the clock, not on every edit: a file write per painted tile
    // would make a drag stutter, and ten seconds is the most that can be lost.
    if (stack_.dirty()) {
        autosave_timer_ += dt;
        if (autosave_timer_ >= kAutosaveSeconds) {
            autosave_timer_ = 0.0;
            doc::write_autosave(path_, tilemap::to_text(map_));
        }
    } else {
        autosave_timer_ = 0.0;
    }

    if (!interactive) {
        // A dialog opened mid-drag must not leave a stroke half-open: finish it, so
        // what was already painted is one undoable step rather than an orphan.
        if (stroke_.active()) if (auto c = stroke_.finish()) stack_.push_apply(*c);
        rect_active_ = false;
        panning_ = false;
        return;
    }

#ifdef __APPLE__
    const bool cmd = in.mods.super;
#else
    const bool cmd = in.mods.ctrl;
#endif
    if (cmd && in.pressed(platform::Key::S)) message_ = save();
    if (cmd && in.pressed(platform::Key::Z)) {
        const bool ok = in.mods.shift ? stack_.redo() : stack_.undo();
        if (!ok) note(false, in.mods.shift ? "nothing to redo" : "nothing to undo");
    }
    if (!cmd) {
        for (int i = 0; i <= 9; ++i) {
            const auto k = static_cast<platform::Key>(static_cast<int>(platform::Key::Num0) + i);
            if (in.pressed(k)) brush_ = i;
        }
        if (in.pressed(platform::Key::B)) tool_ = Tool::Paint;
        if (in.pressed(platform::Key::R)) tool_ = Tool::Rect;
        if (in.pressed(platform::Key::G)) tool_ = Tool::Fill;
    }

    // The canvas rect comes from the previous draw — immediate mode has no layout
    // before it draws. One frame of lag on the very first frame, and none after.
    const ui::Rect c = canvas_;
    const int tile = map_.tile * zoom_;
    if (c.w <= 0 || tile <= 0) return;

    const bool over = in.mouse_x >= c.x && in.mouse_x < c.x + c.w &&
                      in.mouse_y >= c.y && in.mouse_y < c.y + c.h;

    if (in.wheel_y != 0 && over) zoom_ = std::clamp(zoom_ + (in.wheel_y > 0 ? 1 : -1), 1, 8);

    if (over && in.pressed(platform::MouseButton::Middle)) {
        panning_ = true;
        pan_from_x_ = in.mouse_x; pan_from_y_ = in.mouse_y;
        pan_origin_x_ = pan_x_;   pan_origin_y_ = pan_y_;
    }
    if (panning_) {
        pan_x_ = pan_origin_x_ + (in.mouse_x - pan_from_x_);
        pan_y_ = pan_origin_y_ + (in.mouse_y - pan_from_y_);
        if (!in.down(platform::MouseButton::Middle)) panning_ = false;
    }

    // Which tile the pointer is on. Integer division rounds toward zero, so a pixel
    // three to the left of the map would read as column 0 — reject before dividing.
    const int ox = c.x + pan_x_, oy = c.y + pan_y_;
    hover_x_ = hover_y_ = -1;
    if (over && in.mouse_x >= ox && in.mouse_y >= oy) {
        const int tx = (in.mouse_x - ox) / tile, ty = (in.mouse_y - oy) / tile;
        if (map_.in_bounds(tx, ty)) { hover_x_ = tx; hover_y_ = ty; }
    }

    const std::string layer = layer_name();
    if (layer.empty() || panning_) return;

    const bool left  = in.down(platform::MouseButton::Left);
    const bool right = in.down(platform::MouseButton::Right);
    // Right-drag erases. It is the same gesture as painting with brush 0, and having
    // it on a button means the brush does not have to be changed to rub something out.
    const std::int32_t id = right ? 0 : brush_;
    const bool drawing = (left || right) && !cmd;

    switch (tool_) {
        case Tool::Paint:
            if (drawing && !stroke_.active() && hover_x_ >= 0)
                stroke_.begin(map_, layer, id, right ? "erase" : "paint");
            if (stroke_.active() && hover_x_ >= 0) stroke_.touch(hover_x_, hover_y_);
            if (!drawing && stroke_.active())
                if (auto done = stroke_.finish()) stack_.push_apply(*done);
            break;

        case Tool::Rect:
            if (drawing && !rect_active_ && hover_x_ >= 0) {
                rect_active_ = true; rect_x0_ = hover_x_; rect_y0_ = hover_y_;
            } else if (!drawing && rect_active_) {
                rect_active_ = false;
                const int x1 = hover_x_ < 0 ? rect_x0_ : hover_x_;
                const int y1 = hover_y_ < 0 ? rect_y0_ : hover_y_;
                auto edit = mapedit::make_command(
                    map_, layer, mapedit::rect_cells(map_, layer, rect_x0_, rect_y0_, x1, y1, id),
                    right ? "erase rect" : "fill rect");
                if (edit) stack_.push_apply(*edit);
            }
            break;

        case Tool::Fill:
            // A flood is one action on PRESS, not a drag: repeating it every frame the
            // button is held would stack identical no-op steps.
            if ((in.pressed(platform::MouseButton::Left) ||
                 in.pressed(platform::MouseButton::Right)) && hover_x_ >= 0 && !cmd) {
                auto edit = mapedit::make_command(
                    map_, layer, mapedit::flood_cells(map_, layer, hover_x_, hover_y_, id),
                    "flood fill");
                if (edit) stack_.push_apply(*edit);
            }
            break;
    }
}

// ---- drawing -----------------------------------------------------------------

void MapWorkspace::draw_canvas(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) {
    canvas_ = area;
    g.fill_round_rect(area.x, area.y, area.w, area.h, th::radius_md, th::elevated);

    if (!loaded_) {
        g.set_font_size(th::sz_body);
        g.draw_text(area.x + th::space_lg, area.y + th::space_lg, problem_.c_str(), th::warn);
        return;
    }

    const int tile = map_.tile * zoom_;
    // Centre the map the first time it is drawn. Only draw knows the canvas size, and
    // a map pinned to the top-left corner of a large canvas reads as a mistake.
    if (!centred_ && tile > 0) {
        centred_ = true;
        pan_x_ = std::max(0, (area.w - map_.w * tile) / 2);
        pan_y_ = std::max(0, (area.h - map_.h * tile) / 2);
    }
    const int ox = area.x + pan_x_, oy = area.y + pan_y_;

    // Clipped to the canvas, so a panned map cannot paint over the inspector. This is
    // what push_clip was added for.
    g.push_clip(area.x, area.y, area.w, area.h);

    for (const tilemap::Layer& L : map_.layers) {
        const bool mask = L.kind == tilemap::LayerKind::Mask;
        for (int y = 0; y < map_.h; ++y) {
            const int py = oy + y * tile;
            if (py + tile < area.y || py > area.y + area.h) continue;      // cull rows
            for (int x = 0; x < map_.w; ++x) {
                const std::int32_t id = L.cells[static_cast<std::size_t>(y) * map_.w + x];
                if (id == 0) continue;
                const int px = ox + x * tile;
                if (px + tile < area.x || px > area.x + area.w) continue;
                // A mask draws as a translucent wash over the tiles rather than as a
                // colour of its own: it says "this is blocked", not "this looks like".
                if (mask) g.fill_rect_blend(px, py, tile, tile, 0x70E5657A);
                else      g.fill_rect(px, py, tile, tile, tile_color(id));
            }
        }
    }

    // Grid over the tiles, not seams between them. Dropped below zoom 2, where it
    // would be more ink than map.
    if (zoom_ >= 2) {
        for (int x = 0; x <= map_.w; ++x)
            g.fill_rect_blend(ox + x * tile, oy, 1, map_.h * tile, 0x28FFFFFF);
        for (int y = 0; y <= map_.h; ++y)
            g.fill_rect_blend(ox, oy + y * tile, map_.w * tile, 1, 0x28FFFFFF);
    }
    g.draw_rect(ox - 1, oy - 1, map_.w * tile + 2, map_.h * tile + 2, th::border_strong);

    if (rect_active_) {
        const int hx = hover_x_ < 0 ? rect_x0_ : hover_x_;
        const int hy = hover_y_ < 0 ? rect_y0_ : hover_y_;
        const int x0 = std::min(rect_x0_, hx), x1 = std::max(rect_x0_, hx);
        const int y0 = std::min(rect_y0_, hy), y1 = std::max(rect_y0_, hy);
        g.fill_rect_blend(ox + x0 * tile, oy + y0 * tile,
                          (x1 - x0 + 1) * tile, (y1 - y0 + 1) * tile, 0x405AAAE6);
        g.draw_rect(ox + x0 * tile, oy + y0 * tile,
                    (x1 - x0 + 1) * tile, (y1 - y0 + 1) * tile, th::accent);
    } else if (hover_x_ >= 0) {
        g.draw_rect(ox + hover_x_ * tile, oy + hover_y_ * tile, tile, tile, th::accent);
    }

    g.pop_clip();
    (void)ui;
}

void MapWorkspace::draw_inspector(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) {
    g.fill_round_rect(area.x, area.y, area.w, area.h, th::radius_md, th::elevated);
    const ui::Rect inner{area.x + th::space_md, area.y + th::space_md,
                         area.w - th::space_md * 2, area.h - th::space_md * 2};

    ui.push_id("insp");
    ui.begin_layout(inner, ui::Axis::Y, ui::LayoutOpts{th::space_xs, 0});

    g.set_font_size(th::sz_label);
    g.draw_text(inner.x, ui.slot(th::sz_label + th::space_xs).y, "Map", th::text);
    g.set_font_size(th::sz_caption);

    if (!loaded_) {
        g.draw_text(inner.x, ui.slot(th::sz_caption).y, "no map loaded", th::text_muted);
        ui.end_layout();
        ui.pop_id();
        return;
    }

    const std::string dims = std::to_string(map_.w) + " x " + std::to_string(map_.h) +
                             "   " + std::to_string(map_.tile) + "px   zoom x" +
                             std::to_string(zoom_);
    g.draw_text(inner.x, ui.slot(th::sz_caption + th::space_sm).y, dims.c_str(), th::text_dim);

    // ---- tools ----
    g.draw_text(inner.x, ui.slot(th::sz_caption + th::space_xs).y, "TOOL   B / R / G",
                th::text_muted);
    {
        const ui::Rect row = ui.slot(30);
        ui.push_id("tool");
        const int w = (row.w - th::space_xs * 2) / 3;
        for (int i = 0; i < 3; ++i)
            if (ui.button(ui::Rect{row.x + i * (w + th::space_xs), row.y, w, row.h},
                          kToolNames[i], static_cast<int>(tool_) == i))
                want_tool_ = i;
        ui.pop_id();
    }
    ui.skip();

    // ---- layers ----
    g.set_font_size(th::sz_caption);
    g.draw_text(inner.x, ui.slot(th::sz_caption + th::space_xs).y, "LAYERS", th::text_muted);
    ui.push_id("layer");
    for (std::size_t i = 0; i < map_.layers.size(); ++i) {
        const tilemap::Layer& L = map_.layers[i];
        const bool mask = L.kind == tilemap::LayerKind::Mask;
        ui.push_id(static_cast<int>(i));
        if (ui.list_item(ui.slot(26), L.name.c_str(), static_cast<int>(i) == layer_, nullptr,
                         mask ? "mask" : "tiles", mask ? ui::Tone::Danger : ui::Tone::Info))
            want_layer_ = static_cast<int>(i);
        ui.pop_id();
    }
    ui.pop_id();
    ui.skip();

    // ---- brush ----
    g.set_font_size(th::sz_caption);
    g.draw_text(inner.x, ui.slot(th::sz_caption + th::space_xs).y, "BRUSH   0 - 9,  RMB erases",
                th::text_muted);
    {
        const ui::Rect row = ui.slot(26);
        ui.push_id("brush");
        const int w = row.w / 10;
        for (int i = 0; i <= 9; ++i) {
            const ui::Rect r{row.x + i * w, row.y, w - 2, row.h};
            bool hovered = false;
            ui.push_id(i);
            const bool clicked = ui.hit("swatch", r, &hovered);
            ui.pop_id();
            if (clicked) want_brush_ = i;
            g.fill_round_rect(r.x, r.y, r.w, r.h, th::radius_sm, tile_color(i));
            // Id 0 is "empty", and its colour is nearly the panel's — outline it so
            // the row does not read as starting with a gap.
            if (i == 0) g.draw_round_rect(r.x, r.y, r.w, r.h, th::radius_sm, th::border_strong);
            if (i == brush_)
                g.draw_round_rect(r.x - 1, r.y - 1, r.w + 2, r.h + 2, th::radius_sm, th::accent);
            else if (hovered)
                g.draw_round_rect(r.x, r.y, r.w, r.h, th::radius_sm, th::text_dim);
        }
        ui.pop_id();
    }
    ui.skip();

    // ---- history ----
    {
        const ui::Rect row = ui.slot(30);
        ui.push_id("hist");
        const int half = (row.w - th::space_xs) / 2;
        if (ui.button(ui::Rect{row.x, row.y, half, row.h}, "Undo", false, stack_.can_undo()))
            want_undo_ = true;
        if (ui.button(ui::Rect{row.x + half + th::space_xs, row.y, half, row.h}, "Redo", false,
                      stack_.can_redo()))
            want_redo_ = true;
        ui.pop_id();
    }
    if (ui.button(ui.slot(30), dirty() ? "Save  *" : "Save", dirty())) want_save_ = true;

    g.set_font_size(th::sz_caption);
    const std::string hint = stack_.can_undo() ? "undo: " + stack_.undo_label()
                                               : std::string("nothing to undo");
    g.draw_text(inner.x, ui.slot(th::sz_caption + th::space_xs).y, hint.c_str(), th::text_muted);

    ui.end_layout();
    ui.pop_id();
}

} // namespace studioshell
