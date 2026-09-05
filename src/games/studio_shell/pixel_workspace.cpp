// =============================================================================
//  games/studio_shell/pixel_workspace.cpp  —  see pixel_workspace.hpp
// =============================================================================
#include "games/studio_shell/pixel_workspace.hpp"

#include <algorithm>
#include <map>
#include <utility>

#include "engine/commands/registry.hpp"
#include "engine/document/document.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"

namespace studioshell {

namespace th = ui::theme;

namespace {

// Same cadence as the map workspace, for the same reason: long enough that a stroke
// does not write a file per frame, short enough that a crash costs a sentence.
constexpr double kAutosaveSeconds = 10.0;

constexpr const char* kToolNames[] = {"Pencil", "Rect", "Fill", "Pick"};

// The guide grid. 16 is the tile size of every sheet in this repository, and a sheet
// editor without a tile guide is a grid of pixels you have to count.
constexpr int kTileGuide = 16;

// How many colours the sampled palette holds, after the transparent swatch.
constexpr int kPaletteN = 15;

constexpr gfx::Color kTransparent = 0x00000000u;

// The checkerboard behind the image. Without it, a transparent pixel and a dark pixel
// are the same thing on screen — which is exactly the mistake that ships a sprite
// with a black halo.
constexpr gfx::Color kCheckA = 0xFF2A2F3A;
constexpr gfx::Color kCheckB = 0xFF232833;

std::string to_hex(gfx::Color c) {
    static const char* d = "0123456789ABCDEF";
    std::string s = "#";
    for (int shift = 28; shift >= 0; shift -= 4) s += d[(c >> shift) & 0xFu];
    return s;
}

} // namespace

// ---- construction ------------------------------------------------------------

PixelWorkspace::PixelWorkspace(std::vector<std::string> texture_paths)
    : paths_(std::move(texture_paths)) {
    load();
}

PixelWorkspace::~PixelWorkspace() {
    if (!commands_registered_) return;
    // Every handler captured `this`. Leaving them registered would leave the palette
    // holding a call into freed memory (D24).
    for (const char* id : {"pixel.save", "pixel.undo", "pixel.redo", "pixel.reload", "pixel.next"})
        cmd::unregister(id);
}

void PixelWorkspace::load() {
    loaded_ = false;
    problem_.clear();
    stack_.clear();
    recovery_pending_ = false;
    recovery_text_.clear();
    palette_.clear();
    path_.clear();

    if (paths_.empty()) {
        problem_ = "this project declares no texture - add `asset texture <path>` to its manifest";
        return;
    }
    index_ = std::clamp(index_, 0, static_cast<int>(paths_.size()) - 1);
    path_  = paths_[static_cast<std::size_t>(index_)];

    const doc::Opened opened = doc::open(path_);
    if (opened.state == doc::OpenState::Missing) {
        problem_ = "cannot read " + path_;
        return;
    }
    // `.hrt` is binary and doc:: carries content as std::string, which holds NUL bytes
    // like any other. Going through doc:: rather than assets:: directly is what buys
    // autosave and recovery here for free.
    auto decoded = gfx::decode_hrt(std::vector<std::uint8_t>(opened.content.begin(),
                                                             opened.content.end()));
    if (!decoded) {
        problem_ = path_ + " is not a .hrt this build understands";
        return;
    }
    img_     = std::move(*decoded);
    loaded_  = true;
    centred_ = false;   // recentre on the new image's size, not the old one
    build_palette();
    if (opened.state == doc::OpenState::RecoveryOffered) {
        recovery_pending_ = true;
        recovery_text_    = opened.recovered;
    }
}

// The palette is the image's own most-used colours. A fixed ramp would be wrong for
// every sheet: editing Kenney's tiles with a generic rainbow means every stroke is
// visibly foreign, and matching a colour by eye from a swatch grid is the slowest
// part of pixel art.
void PixelWorkspace::build_palette() {
    palette_.clear();
    palette_.push_back(kTransparent);            // swatch 0 is always the eraser
    std::map<gfx::Color, int> counts;
    for (gfx::Color c : img_.pixels) ++counts[c];

    std::vector<std::pair<int, gfx::Color>> ranked;
    ranked.reserve(counts.size());
    for (const auto& [c, n] : counts)
        if (c != kTransparent) ranked.push_back({n, c});
    // Ties broken by colour value, not by map order, so the palette is the same on
    // every machine — a swatch that moves between runs is a swatch nobody learns.
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    for (std::size_t i = 0; i < ranked.size() && i < kPaletteN; ++i)
        palette_.push_back(ranked[i].second);
    if (palette_.size() > 1) colour_ = palette_[1];
}

void PixelWorkspace::take_recovery() {
    recovery_pending_ = false;
    auto decoded = gfx::decode_hrt(std::vector<std::uint8_t>(recovery_text_.begin(),
                                                             recovery_text_.end()));
    recovery_text_.clear();
    if (!decoded) { note(false, "the autosave could not be read; kept the saved file"); return; }

    // Recovery is an EDIT, not a load: it goes on the undo stack like any other, so
    // the document opens dirty and one Ctrl+Z returns to what was deliberately saved.
    gfx::Image* target = &img_;
    gfx::Image  before = img_;
    gfx::Image  after  = std::move(*decoded);
    stack_.push_apply(doc::Command{"recover unsaved changes",
                                   [target, after] { *target = after; },
                                   [target, before] { *target = before; },
                                   0});
    build_palette();
    note(true, "recovered unsaved changes - not yet saved");
}

void PixelWorkspace::dismiss_recovery() {
    recovery_pending_ = false;
    recovery_text_.clear();
    note(true, "kept the saved version; the autosave is still on disk");
}

// ---- commands ----------------------------------------------------------------

void PixelWorkspace::register_commands() {
    const auto did = [](bool ok, const char* yes, const char* no) {
        return engine::OpResult{ok, ok ? yes : no};
    };
    cmd::register_command(cmd::Info{"pixel.save", "Pixels: save", "Cmd+S", ""},
                          [this](const std::vector<std::string>&) { return save(); });
    cmd::register_command(cmd::Info{"pixel.undo", "Pixels: undo", "Cmd+Z", ""},
                          [this, did](const std::vector<std::string>&) {
                              return did(stack_.undo(), "undone", "nothing to undo");
                          });
    cmd::register_command(cmd::Info{"pixel.redo", "Pixels: redo", "Shift+Cmd+Z", ""},
                          [this, did](const std::vector<std::string>&) {
                              return did(stack_.redo(), "redone", "nothing to redo");
                          });
    cmd::register_command(cmd::Info{"pixel.reload", "Pixels: reload from disk", "", ""},
                          [this](const std::vector<std::string>&) { return reload(); });
    // The same operation the inspector's texture list performs. A project has several
    // sheets and the keyboard has to reach them too — and routing both through
    // open_index is what stops the button and the command from drifting apart.
    cmd::register_command(cmd::Info{"pixel.next", "Pixels: next texture", "", ""},
                          [this](const std::vector<std::string>&) {
                              if (paths_.size() < 2) return engine::OpResult{false, "only one texture"};
                              return open_index((index_ + 1) % static_cast<int>(paths_.size()));
                          });
    commands_registered_ = true;
}

engine::OpResult PixelWorkspace::save() {
    if (!loaded_) return engine::OpResult{false, "no image to save"};
    const std::vector<std::uint8_t> bytes = gfx::encode_hrt(img_);
    if (!doc::save(path_, std::string(bytes.begin(), bytes.end())))
        return engine::OpResult{false, "could not write " + path_};
    stack_.mark_saved();
    autosave_timer_ = 0.0;
    return engine::OpResult{true, "saved " + path_};
}

engine::OpResult PixelWorkspace::open_index(int i) {
    if (i < 0 || i >= static_cast<int>(paths_.size()))
        return {false, "no such texture"};
    if (i == index_) return {true, "already open: " + path_};
    // Switching away from unsaved work would lose it silently, and there is no undo
    // across a reload. Refuse and say what to do: save or undo, either is better than
    // a surprise.
    if (dirty()) return {false, "save or undo first - switching would discard changes"};
    index_ = i;
    load();
    return loaded_ ? engine::OpResult{true, "opened " + path_} : engine::OpResult{false, problem_};
}

engine::OpResult PixelWorkspace::reload() {
    load();
    return loaded_ ? engine::OpResult{true, "reloaded " + path_}
                   : engine::OpResult{false, problem_};
}

void PixelWorkspace::note(bool ok, std::string msg) {
    message_ = engine::OpResult{ok, std::move(msg)};
}

std::string PixelWorkspace::status() const {
    if (!loaded_) return problem_.empty() ? std::string("no texture") : problem_;
    std::string s = path_ + (dirty() ? "  *  unsaved" : "  saved");
    if (hover_x_ >= 0) {
        s += "   " + std::to_string(hover_x_) + ", " + std::to_string(hover_y_);
        // The colour UNDER the cursor, not the selected one: matching a neighbour is
        // most of the work, and reading it off the status bar beats guessing.
        s += "   " + to_hex(img_.pixels[static_cast<std::size_t>(hover_y_) *
                                            static_cast<std::size_t>(img_.w) +
                                        static_cast<std::size_t>(hover_x_)]);
    }
    return s;
}

const char* PixelWorkspace::hint() const {
    return "Cmd+K commands   Cmd+S save   Cmd+Z undo   B/R/G/I tools   MMB pan   wheel zoom";
}

std::optional<engine::OpResult> PixelWorkspace::take_message() {
    auto m = message_;
    message_.reset();
    return m;
}

ui::Rect PixelWorkspace::pixel_rect(int px, int py) const {
    if (canvas_.w <= 0 || zoom_ <= 0) return ui::Rect{};
    return ui::Rect{canvas_.x + pan_x_ + px * zoom_, canvas_.y + pan_y_ + py * zoom_, zoom_, zoom_};
}

// ---- update ------------------------------------------------------------------

void PixelWorkspace::update(double dt, const platform::InputState& in, bool interactive) {
    // Inspector clicks resolved during the previous draw, where the layout is known.
    if (want_tool_ >= 0) { tool_ = static_cast<Tool>(want_tool_); want_tool_ = -1; }
    if (want_swatch_ >= 0) {
        if (want_swatch_ < static_cast<int>(palette_.size()))
            colour_ = palette_[static_cast<std::size_t>(want_swatch_)];
        want_swatch_ = -1;
    }
    if (want_index_ >= 0) {
        const int next = want_index_;
        want_index_ = -1;
        if (next != index_) message_ = open_index(next);
    }
    if (want_undo_) { want_undo_ = false; if (!stack_.undo()) note(false, "nothing to undo"); }
    if (want_redo_) { want_redo_ = false; if (!stack_.redo()) note(false, "nothing to redo"); }
    if (want_save_) { want_save_ = false; message_ = save(); }

    if (!loaded_) return;

    if (stack_.dirty()) {
        autosave_timer_ += dt;
        if (autosave_timer_ >= kAutosaveSeconds) {
            autosave_timer_ = 0.0;
            const std::vector<std::uint8_t> bytes = gfx::encode_hrt(img_);
            doc::write_autosave(path_, std::string(bytes.begin(), bytes.end()));
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
        last_x_ = last_y_ = -1;
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
        if (in.pressed(platform::Key::B)) tool_ = Tool::Pencil;
        if (in.pressed(platform::Key::R)) tool_ = Tool::Rect;
        if (in.pressed(platform::Key::G)) tool_ = Tool::Fill;
        if (in.pressed(platform::Key::I)) tool_ = Tool::Pick;
    }

    const ui::Rect c = canvas_;
    if (c.w <= 0 || zoom_ <= 0) return;

    const bool over = in.mouse_x >= c.x && in.mouse_x < c.x + c.w &&
                      in.mouse_y >= c.y && in.mouse_y < c.y + c.h;

    // Wider zoom range than the map editor: this is the tool where 1:1 is too small to
    // aim in and a 16px tile wants most of the canvas.
    if (in.wheel_y != 0 && over) zoom_ = std::clamp(zoom_ + (in.wheel_y > 0 ? 1 : -1), 1, 32);

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

    // Integer division rounds toward zero, so a pixel three to the left of the image
    // would read as column 0 — reject before dividing.
    const int ox = c.x + pan_x_, oy = c.y + pan_y_;
    hover_x_ = hover_y_ = -1;
    if (over && in.mouse_x >= ox && in.mouse_y >= oy) {
        const int px = (in.mouse_x - ox) / zoom_, py = (in.mouse_y - oy) / zoom_;
        if (px < img_.w && py < img_.h) { hover_x_ = px; hover_y_ = py; }
    }

    if (panning_) return;

    const bool left  = in.down(platform::MouseButton::Left);
    const bool right = in.down(platform::MouseButton::Right);
    // Right-drag erases, exactly as right-drag paints id 0 in the map editor: the
    // eraser should not require changing the colour and changing it back.
    const gfx::Color paint_colour = right ? kTransparent : colour_;
    const bool       drawing = (left || right) && !cmd;

    switch (tool_) {
        case Tool::Pencil:
            if (drawing && !stroke_.active() && hover_x_ >= 0) {
                stroke_.begin(img_, paint_colour, right ? "erase" : "draw");
                last_x_ = last_y_ = -1;
            }
            if (stroke_.active() && hover_x_ >= 0) {
                // Interpolate from the previous frame's pixel. At zoom 8 an ordinary
                // gesture moves several image pixels per frame, and touching only the
                // current one draws a dotted line.
                if (last_x_ >= 0) stroke_.touch_line(last_x_, last_y_, hover_x_, hover_y_);
                else              stroke_.touch(hover_x_, hover_y_);
                last_x_ = hover_x_; last_y_ = hover_y_;
            }
            if (!drawing && stroke_.active()) {
                if (auto done = stroke_.finish()) stack_.push_apply(*done);
                last_x_ = last_y_ = -1;
            }
            break;

        case Tool::Rect:
            if (drawing && !rect_active_ && hover_x_ >= 0) {
                rect_active_ = true; rect_x0_ = hover_x_; rect_y0_ = hover_y_;
            } else if (!drawing && rect_active_) {
                rect_active_ = false;
                const int x1 = hover_x_ < 0 ? rect_x0_ : hover_x_;
                const int y1 = hover_y_ < 0 ? rect_y0_ : hover_y_;
                auto edit = paint::make_command(
                    img_, paint::rect_pixels(img_, rect_x0_, rect_y0_, x1, y1, paint_colour),
                    right ? "erase rect" : "fill rect");
                if (edit) stack_.push_apply(*edit);
            }
            break;

        case Tool::Fill:
            // One action on PRESS, not a drag: repeating it every frame the button is
            // held would stack identical no-op steps.
            if ((in.pressed(platform::MouseButton::Left) ||
                 in.pressed(platform::MouseButton::Right)) && hover_x_ >= 0 && !cmd) {
                auto edit = paint::make_command(
                    img_, paint::flood_pixels(img_, hover_x_, hover_y_, paint_colour),
                    "flood fill");
                if (edit) stack_.push_apply(*edit);
            }
            break;

        case Tool::Pick:
            // The eyedropper changes the SELECTED colour and never the image, so it is
            // not an undoable step and must not become one.
            if (in.pressed(platform::MouseButton::Left) && hover_x_ >= 0 && !cmd) {
                colour_ = img_.pixels[static_cast<std::size_t>(hover_y_) *
                                          static_cast<std::size_t>(img_.w) +
                                      static_cast<std::size_t>(hover_x_)];
                tool_ = Tool::Pencil;   // pick, then keep drawing: the gesture is one thought
            }
            break;
    }
}

// ---- drawing -----------------------------------------------------------------

void PixelWorkspace::draw_canvas(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) {
    canvas_ = area;
    g.fill_round_rect(area.x, area.y, area.w, area.h, th::radius_md, th::elevated);

    if (!loaded_) {
        g.set_font_size(th::sz_body);
        g.draw_text(area.x + th::space_lg, area.y + th::space_lg, problem_.c_str(), th::warn);
        return;
    }

    // Centre the image the first time it is drawn. Only draw knows the canvas size.
    if (!centred_) {
        centred_ = true;
        pan_x_ = std::max(0, (area.w - img_.w * zoom_) / 2);
        pan_y_ = std::max(0, (area.h - img_.h * zoom_) / 2);
    }
    const int ox = area.x + pan_x_, oy = area.y + pan_y_;

    g.push_clip(area.x, area.y, area.w, area.h);

    // The checkerboard, in image space so it scrolls with the picture. Two 8-logical-
    // pixel squares regardless of zoom would drift against the image and read as a
    // second, wrong grid.
    const int check = std::max(1, 8 / std::max(1, zoom_ / 4));
    for (int y = 0; y < img_.h; ++y) {
        const int py = oy + y * zoom_;
        if (py + zoom_ < area.y || py > area.y + area.h) continue;
        for (int x = 0; x < img_.w; ++x) {
            const int px = ox + x * zoom_;
            if (px + zoom_ < area.x || px > area.x + area.w) continue;
            const gfx::Color under = ((x / check) + (y / check)) % 2 ? kCheckA : kCheckB;
            g.fill_rect(px, py, zoom_, zoom_, under);
            const gfx::Color c = img_.pixels[static_cast<std::size_t>(y) *
                                                 static_cast<std::size_t>(img_.w) +
                                             static_cast<std::size_t>(x)];
            // Blended, not copied: a half-transparent pixel must show the board
            // through it, or the editor lies about the alpha it is about to save.
            if ((c >> 24) != 0) g.fill_rect_blend(px, py, zoom_, zoom_, c);
        }
    }

    // Tile guide before pixel grid, so the heavier line wins where they coincide.
    if (zoom_ >= 3) {
        for (int x = 0; x <= img_.w; x += kTileGuide)
            g.fill_rect_blend(ox + x * zoom_, oy, 1, img_.h * zoom_, 0x60FFFFFF);
        for (int y = 0; y <= img_.h; y += kTileGuide)
            g.fill_rect_blend(ox, oy + y * zoom_, img_.w * zoom_, 1, 0x60FFFFFF);
    }
    // The pixel grid only above zoom 8: below that it is more ink than image.
    if (zoom_ >= 8) {
        for (int x = 0; x <= img_.w; ++x)
            g.fill_rect_blend(ox + x * zoom_, oy, 1, img_.h * zoom_, 0x1AFFFFFF);
        for (int y = 0; y <= img_.h; ++y)
            g.fill_rect_blend(ox, oy + y * zoom_, img_.w * zoom_, 1, 0x1AFFFFFF);
    }
    g.draw_rect(ox - 1, oy - 1, img_.w * zoom_ + 2, img_.h * zoom_ + 2, th::border_strong);

    if (rect_active_) {
        const int hx = hover_x_ < 0 ? rect_x0_ : hover_x_;
        const int hy = hover_y_ < 0 ? rect_y0_ : hover_y_;
        const int x0 = std::min(rect_x0_, hx), x1 = std::max(rect_x0_, hx);
        const int y0 = std::min(rect_y0_, hy), y1 = std::max(rect_y0_, hy);
        g.fill_rect_blend(ox + x0 * zoom_, oy + y0 * zoom_,
                          (x1 - x0 + 1) * zoom_, (y1 - y0 + 1) * zoom_, 0x405AAAE6);
        g.draw_rect(ox + x0 * zoom_, oy + y0 * zoom_,
                    (x1 - x0 + 1) * zoom_, (y1 - y0 + 1) * zoom_, th::accent);
    } else if (hover_x_ >= 0) {
        g.draw_rect(ox + hover_x_ * zoom_, oy + hover_y_ * zoom_, zoom_, zoom_, th::accent);
    }

    g.pop_clip();
    (void)ui;
}

void PixelWorkspace::draw_inspector(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) {
    g.fill_round_rect(area.x, area.y, area.w, area.h, th::radius_md, th::elevated);
    const ui::Rect inner{area.x + th::space_md, area.y + th::space_md,
                         area.w - th::space_md * 2, area.h - th::space_md * 2};

    ui.push_id("pixinsp");
    ui.begin_layout(inner, ui::Axis::Y, ui::LayoutOpts{th::space_xs, 0});

    g.set_font_size(th::sz_label);
    g.draw_text(inner.x, ui.slot(th::sz_label + th::space_xs).y, "Pixels", th::text);
    g.set_font_size(th::sz_caption);

    if (!loaded_) {
        g.draw_text(inner.x, ui.slot(th::sz_caption).y, "no texture loaded", th::text_muted);
        ui.end_layout();
        ui.pop_id();
        return;
    }

    const std::string dims = std::to_string(img_.w) + " x " + std::to_string(img_.h) +
                             "   zoom x" + std::to_string(zoom_);
    g.draw_text(inner.x, ui.slot(th::sz_caption + th::space_sm).y, dims.c_str(), th::text_dim);

    // ---- tools ----
    g.draw_text(inner.x, ui.slot(th::sz_caption + th::space_xs).y, "TOOL   B / R / G / I",
                th::text_muted);
    {
        const ui::Rect row = ui.slot(30);
        ui.push_id("tool");
        const int w = (row.w - th::space_xs * 3) / 4;
        for (int i = 0; i < 4; ++i)
            if (ui.button(ui::Rect{row.x + i * (w + th::space_xs), row.y, w, row.h},
                          kToolNames[i], static_cast<int>(tool_) == i))
                want_tool_ = i;
        ui.pop_id();
    }
    ui.skip();

    // ---- palette ----
    g.set_font_size(th::sz_caption);
    g.draw_text(inner.x, ui.slot(th::sz_caption + th::space_xs).y,
                "COLOUR   from this image,  RMB erases", th::text_muted);
    {
        const int per_row = 8;
        ui.push_id("swatch");
        for (int base = 0; base < static_cast<int>(palette_.size()); base += per_row) {
            const ui::Rect row = ui.slot(26);
            const int w = row.w / per_row;
            for (int i = base; i < base + per_row && i < static_cast<int>(palette_.size()); ++i) {
                const ui::Rect r{row.x + (i - base) * w, row.y, w - 2, row.h};
                bool hovered = false;
                ui.push_id(i);
                const bool clicked = ui.hit("sw", r, &hovered);
                ui.pop_id();
                if (clicked) want_swatch_ = i;
                const gfx::Color c = palette_[static_cast<std::size_t>(i)];
                if ((c >> 24) == 0) {
                    // The eraser swatch has nothing to show, so it shows the board it
                    // will reveal rather than a black square that reads as a colour.
                    g.fill_round_rect(r.x, r.y, r.w, r.h, th::radius_sm, kCheckB);
                    g.fill_rect(r.x + r.w / 2, r.y, r.w - r.w / 2, r.h / 2, kCheckA);
                    g.fill_rect(r.x, r.y + r.h / 2, r.w / 2, r.h - r.h / 2, kCheckA);
                } else {
                    g.fill_round_rect(r.x, r.y, r.w, r.h, th::radius_sm, c);
                }
                if (c == colour_)
                    g.draw_round_rect(r.x - 1, r.y - 1, r.w + 2, r.h + 2, th::radius_sm, th::accent);
                else if (hovered)
                    g.draw_round_rect(r.x, r.y, r.w, r.h, th::radius_sm, th::text_dim);
            }
        }
        ui.pop_id();
    }
    g.set_font_size(th::sz_caption);
    g.draw_text(inner.x, ui.slot(th::sz_caption + th::space_sm).y, to_hex(colour_).c_str(),
                th::text_dim);

    // ---- which texture ----
    if (paths_.size() > 1) {
        g.draw_text(inner.x, ui.slot(th::sz_caption + th::space_xs).y, "TEXTURE", th::text_muted);
        ui.push_id("tex");
        for (std::size_t i = 0; i < paths_.size(); ++i) {
            ui.push_id(static_cast<int>(i));
            if (ui.list_item(ui.slot(24), paths_[i].c_str(), static_cast<int>(i) == index_))
                want_index_ = static_cast<int>(i);
            ui.pop_id();
        }
        ui.pop_id();
        ui.skip();
    }

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
