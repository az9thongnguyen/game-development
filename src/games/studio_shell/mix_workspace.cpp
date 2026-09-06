// =============================================================================
//  games/studio_shell/mix_workspace.cpp
// =============================================================================
#include "games/studio_shell/mix_workspace.hpp"

#include <algorithm>
#include <cstdio>

#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/document/document.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"

namespace studioshell {

namespace th = ui::theme;

namespace {

constexpr double kAutosaveSeconds = 10.0;

// The ramp a swap cycles through. Deliberately SHORT and deliberately fixed: the whole
// argument for a parts mixer is that the answers are a closed set, and a colour picker
// with sixteen million answers would hand back exactly the freedom this tool trades
// away. Six recolours is enough for a village and few enough to click through.
const gfx::Color kRamp[] = {
    gfx::rgb(0xb5, 0x56, 0x6e), gfx::rgb(0x6e, 0x8f, 0xb5), gfx::rgb(0x5a, 0xa0, 0x72),
    gfx::rgb(0xc8, 0x9b, 0x4a), gfx::rgb(0x8a, 0x6f, 0xb5), gfx::rgb(0xd9, 0xd2, 0xc4),
};
constexpr int kRampCount = static_cast<int>(sizeof(kRamp) / sizeof(kRamp[0]));

std::string sibling_mix(const std::string& hrt) {
    const auto dot = hrt.rfind('.');
    return (dot == std::string::npos ? hrt : hrt.substr(0, dot)) + ".mix";
}

} // namespace

MixWorkspace::MixWorkspace(std::vector<std::string> texture_paths) {
    // The openable set is DERIVED from the sibling rule, not listed separately: a
    // second list is a second thing to keep in step with provenance_core, and the day
    // they disagree the editor shows a file the ledger does not believe in.
    for (const std::string& t : texture_paths) {
        const std::string m = sibling_mix(t);
        if (assets::load_file(m)) paths_.push_back(m);
    }
    load();
}

MixWorkspace::~MixWorkspace() {
    if (!commands_registered_) return;
    for (const char* id : {"mix.save", "mix.undo", "mix.redo", "mix.reload", "mix.bake"})
        cmd::unregister(id);
}

void MixWorkspace::note(bool ok, std::string msg) {
    message_ = engine::OpResult{ok, std::move(msg)};
}

const mix::Mix::Sheet* MixWorkspace::first_sheet() const {
    return mix_.sheets.empty() ? nullptr : &mix_.sheets.front();
}

void MixWorkspace::install(const std::string& text) {
    std::string why;
    auto        m = mix::parse_mix(text, &why);
    if (!m) { problem_ = why; loaded_ = false; return; }
    mix_ = std::move(*m);
    sheets_.clear();
    for (const mix::Mix::Sheet& sh : mix_.sheets)
        if (auto img = gfx::load_image(sh.path)) sheets_.emplace(sh.name, std::move(*img));
    recompose();
}

void MixWorkspace::load() {
    stack_.clear();
    problem_.clear();
    loaded_ = false;
    picked_ = 0;
    if (paths_.empty()) { problem_ = "this project has no .mix sources"; return; }
    if (which_ >= paths_.size()) which_ = 0;
    path_ = paths_[which_];

    // Through doc::open, so the autosave and the recovery offer come for free and
    // behave the way the other three workspaces do.
    const doc::Opened opened = doc::open(path_);
    if (opened.state == doc::OpenState::Missing) { problem_ = "cannot read " + path_; return; }
    install(opened.content);
    if (!mix_.parts.empty()) loaded_ = true;

    // An autosave newer than the file is OFFERED, never applied: taking it without
    // asking is how someone loses the version they deliberately saved.
    if (opened.state == doc::OpenState::RecoveryOffered) {
        recovery_pending_ = true;
        recovery_text_    = opened.recovered;
    }
}

void MixWorkspace::recompose() {
    const auto find = [this](const std::string& n) -> const gfx::Image* {
        const auto it = sheets_.find(n);
        return it == sheets_.end() ? nullptr : &it->second;
    };
    std::string why;
    if (auto img = mix::compose(mix_, find, &why)) {
        preview_ = std::move(*img);
        problem_.clear();
    } else {
        // A composition that stopped working is shown as a problem and NOT as a blank
        // canvas: an empty preview and a broken one look identical, and only one of
        // them is something you should keep editing.
        problem_ = why;
    }
}

void MixWorkspace::commit(const std::string& before, std::string label) {
    const std::string after = mix::to_text(mix_);
    if (after == before) return;              // history you did not make
    doc::Command cmd;
    cmd.label  = std::move(label);
    cmd.apply  = [this, after]  { install(after);  };
    cmd.revert = [this, before] { install(before); };
    stack_.push_apply(std::move(cmd));
}

void MixWorkspace::mark(const char* id, ui::Rect r) { controls_[id] = r; }

ui::Rect MixWorkspace::control_rect(const char* id) const {
    const auto it = controls_.find(id);
    return it == controls_.end() ? ui::Rect{} : it->second;
}

int MixWorkspace::sheet_tiles() const {
    const mix::Mix::Sheet* sh = first_sheet();
    if (sh == nullptr) return 0;
    const auto it = sheets_.find(sh->name);
    return it == sheets_.end() ? 0 : mix::tile_count(it->second, sh->tile);
}

ui::Rect MixWorkspace::part_rect(int index) const {
    const mix::Mix::Sheet* sh = first_sheet();
    if (sh == nullptr || index < 0 || index >= sheet_tiles()) return {};
    const int side = sh->tile * strip_scale_;
    return ui::Rect{strip_.x + index * (side + th::space_xs), strip_.y, side, side};
}

ui::Rect MixWorkspace::preview_pixel_rect(int x, int y) const {
    if (x < 0 || y < 0 || x >= preview_.w || y >= preview_.h) return {};
    return ui::Rect{preview_rect_.x + x * preview_scale_, preview_rect_.y + y * preview_scale_,
                    preview_scale_, preview_scale_};
}

// ---- the operations ---------------------------------------------------------

engine::OpResult MixWorkspace::add_part(int index) {
    const mix::Mix::Sheet* sh = first_sheet();
    if (sh == nullptr) return {false, "this mix declares no sheet"};
    if (index < 0 || index >= sheet_tiles()) return {false, "no such part"};
    const std::string before = mix::to_text(mix_);
    mix_.parts.push_back(mix::Mix::Part{sh->name, index, 0, 0});
    recompose();
    commit(before, "add part " + std::to_string(index));
    return {true, "added part " + std::to_string(index)};
}

engine::OpResult MixWorkspace::remove_top() {
    if (mix_.parts.size() <= 1)
        return {false, "a mix needs at least one part"};
    const std::string before = mix::to_text(mix_);
    mix_.parts.pop_back();
    recompose();
    commit(before, "remove part");
    return {true, "removed the top part"};
}

engine::OpResult MixWorkspace::cycle_swap(gfx::Color from) {
    if (gfx::a_of(from) == 0) return {false, "nothing there to recolour"};
    const gfx::Color key = from | 0xFF000000u;
    const std::string before = mix::to_text(mix_);

    // Find a swap that already TARGETS this colour, so cycling moves it on rather than
    // stacking a second swap the first one feeds. Two swaps in a chain would make the
    // ramp order matter, which is the sort of rule nobody can see in a picture.
    for (std::size_t i = 0; i < mix_.swaps.size(); ++i) {
        if ((mix_.swaps[i].to & 0x00FFFFFFu) != (key & 0x00FFFFFFu)) continue;
        int at = 0;
        for (int k = 0; k < kRampCount; ++k)
            if ((kRamp[k] & 0x00FFFFFFu) == (key & 0x00FFFFFFu)) { at = k + 1; break; }
        if (at >= kRampCount) {
            // Round the ramp and back to the original: a cycle you cannot leave is a
            // one-way door, and the original colour is the answer people want back.
            mix_.swaps.erase(mix_.swaps.begin() + static_cast<long>(i));
        } else {
            mix_.swaps[i].to = kRamp[at];
        }
        recompose();
        commit(before, "recolour");
        return {true, "recoloured"};
    }

    mix_.swaps.push_back(mix::Mix::Swap{key, kRamp[0] == key ? kRamp[1] : kRamp[0]});
    recompose();
    commit(before, "recolour");
    return {true, "recoloured"};
}

engine::OpResult MixWorkspace::bake() {
    if (path_.empty()) return {false, "nothing open"};
    // Through the registry, not through a second copy of the compose-and-write dance:
    // the CLI and this button must produce the same bytes or the `.hrt` in the repo
    // depends on which one somebody used (D-rule, chapter 111).
    const auto dot = path_.rfind('.');
    const std::string dst = (dot == std::string::npos ? path_ : path_.substr(0, dot)) + ".hrt";
    return cmd::run("asset.mix", {path_, dst});
}

engine::OpResult MixWorkspace::next_mix() {
    if (paths_.size() < 2) return {false, "this project has one mix"};
    if (dirty()) return {false, "save first — switching would drop the edit"};
    which_ = (which_ + 1) % paths_.size();
    load();
    return {true, "opened " + path_};
}

engine::OpResult MixWorkspace::save() {
    if (path_.empty()) return {false, "nothing open"};
    const std::string text = mix::to_text(mix_);
    if (!doc::save(path_, text)) return {false, "cannot write " + path_};
    stack_.mark_saved();
    return {true, "saved " + path_ + "  (Bake writes the .hrt)"};
}

engine::OpResult MixWorkspace::reload() {
    load();
    return loaded_ ? engine::OpResult{true, "reloaded " + path_}
                   : engine::OpResult{false, problem_};
}

std::optional<engine::OpResult> MixWorkspace::take_message() {
    auto m = message_;
    message_.reset();
    return m;
}

void MixWorkspace::take_recovery() {
    recovery_pending_ = false;
    const std::string before = mix::to_text(mix_);
    install(recovery_text_);
    commit(before, "recover");
    recovery_text_.clear();
}

void MixWorkspace::dismiss_recovery() {
    recovery_pending_ = false;
    recovery_text_.clear();
    doc::discard_autosave(path_);
}

void MixWorkspace::register_commands() {
    const auto did = [](bool ok, const char* yes, const char* no) {
        return engine::OpResult{ok, ok ? yes : no};
    };
    cmd::register_command(cmd::Info{"mix.save", "Mixer: save the .mix", "Cmd+S", ""},
                          [this](const std::vector<std::string>&) { return save(); });
    cmd::register_command(cmd::Info{"mix.bake", "Mixer: bake the .hrt", "", ""},
                          [this](const std::vector<std::string>&) { return bake(); });
    cmd::register_command(cmd::Info{"mix.undo", "Mixer: undo", "Cmd+Z", ""},
                          [this, did](const std::vector<std::string>&) {
                              return did(stack_.undo(), "undone", "nothing to undo");
                          });
    cmd::register_command(cmd::Info{"mix.redo", "Mixer: redo", "Shift+Cmd+Z", ""},
                          [this, did](const std::vector<std::string>&) {
                              return did(stack_.redo(), "redone", "nothing to redo");
                          });
    cmd::register_command(cmd::Info{"mix.reload", "Mixer: reload from disk", "", ""},
                          [this](const std::vector<std::string>&) { return reload(); });
    commands_registered_ = true;
}

std::string MixWorkspace::status() const {
    if (!loaded_) return problem_.empty() ? "no mix open" : problem_;
    char buf[192];
    std::snprintf(buf, sizeof buf, "%s   %zu part(s)   %zu swap(s)   %dx%d%s",
                  path_.c_str(), mix_.parts.size(), mix_.swaps.size(), mix_.w, mix_.h,
                  dirty() ? "   *" : "");
    std::string s = buf;
    if (!problem_.empty()) s += "   [" + problem_ + "]";
    return s;
}

const char* MixWorkspace::hint() const {
    return "click a part to add it   click the sprite to recolour   Cmd+S saves the source";
}

void MixWorkspace::update(double dt, const platform::InputState& in, bool interactive) {
    if (want_part_ >= 0)  { const int i = want_part_; want_part_ = -1; message_ = add_part(i); }
    if (want_remove_)     { want_remove_ = false; message_ = remove_top(); }
    if (want_swap_)       { want_swap_ = false; message_ = cycle_swap(picked_); }
    if (want_bake_)       { want_bake_ = false; message_ = bake(); }
    if (want_save_)       { want_save_ = false; message_ = save(); }
    if (want_next_)       { want_next_ = false; message_ = next_mix(); }
    if (want_undo_)       { want_undo_ = false; if (!stack_.undo()) note(false, "nothing to undo"); }
    if (want_redo_)       { want_redo_ = false; if (!stack_.redo()) note(false, "nothing to redo"); }

    if (interactive && in.pressed(platform::MouseButton::Left)) {
        // The strip first: a part is a bigger target than a pixel and the two regions
        // do not overlap, so asking in this order costs nothing and reads as "the
        // thing under the cursor", which is what a hand expects.
        bool hit_strip = false;
        for (int i = 0, n = sheet_tiles(); i < n; ++i) {
            const ui::Rect r = part_rect(i);
            if (r.w <= 0) continue;
            if (in.mouse_x < r.x || in.mouse_x >= r.x + r.w) continue;
            if (in.mouse_y < r.y || in.mouse_y >= r.y + r.h) continue;
            want_part_ = i;
            hit_strip  = true;
            break;
        }
        // Sampling a colour off the PREVIEW, not off a part: a swap acts on the
        // finished sprite, so the colour you point at is the colour it changes.
        const int px = (in.mouse_x - preview_rect_.x) / std::max(1, preview_scale_);
        const int py = (in.mouse_y - preview_rect_.y) / std::max(1, preview_scale_);
        if (!hit_strip && px >= 0 && py >= 0 && px < preview_.w && py < preview_.h &&
            in.mouse_x >= preview_rect_.x && in.mouse_y >= preview_rect_.y) {
            picked_ = preview_.pixels[static_cast<std::size_t>(py) * preview_.w + px];
            want_swap_ = true;
        }
    }

    if (!path_.empty() && dirty()) {
        autosave_timer_ += dt;
        if (autosave_timer_ >= kAutosaveSeconds) {
            autosave_timer_ = 0.0;
            doc::write_autosave(path_, mix::to_text(mix_));
        }
    }
}

void MixWorkspace::draw_canvas(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) {
    (void)ui;
    canvas_ = area;
    g.fill_round_rect(area.x, area.y, area.w, area.h, th::radius_md, th::elevated);
    g.push_clip(area.x, area.y, area.w, area.h);

    const mix::Mix::Sheet* sh = first_sheet();
    const int tiles = sheet_tiles();

    // ---- the parts strip, along the bottom --------------------------------
    // Below the sprite rather than beside it, because the question it answers ("what
    // else could go on") is the one you ask AFTER looking at what you have.
    strip_scale_ = 2;
    const int side = sh ? sh->tile * strip_scale_ : 0;
    strip_ = ui::Rect{area.x + th::space_md, area.y + area.h - th::space_md - side,
                      area.w - th::space_md * 2, side};

    // ---- the sprite, as big as the room above the strip allows ------------
    const int room_h = strip_.y - area.y - th::space_md * 2;
    const int room_w = area.w - th::space_md * 2;
    preview_scale_ = 1;
    if (preview_.w > 0 && preview_.h > 0)
        preview_scale_ = std::max(1, std::min(room_w / preview_.w, room_h / preview_.h));
    const int pw = preview_.w * preview_scale_, ph = preview_.h * preview_scale_;
    preview_rect_ = ui::Rect{area.x + (area.w - pw) / 2, area.y + th::space_md + (room_h - ph) / 2,
                             pw, ph};

    // A chequerboard behind it, in MID greys. Transparent and "black" are the same
    // picture on a dark panel, and on a sprite with a silhouette that is the
    // difference that matters — but two dark greys hide the dark art instead, which
    // is what the first screenshot of this panel showed: a character with no legs.
    // Mid-tones are the only pair both ends of a palette read against.
    const gfx::Color kCheckA = gfx::rgb(0x6a, 0x6e, 0x78);
    const gfx::Color kCheckB = gfx::rgb(0x55, 0x59, 0x62);
    for (int y = 0; y < preview_.h; ++y)
        for (int x = 0; x < preview_.w; ++x)
            g.fill_rect(preview_rect_.x + x * preview_scale_, preview_rect_.y + y * preview_scale_,
                        preview_scale_, preview_scale_,
                        ((x / 2 + y / 2) & 1) ? kCheckA : kCheckB);
    for (int y = 0; y < preview_.h; ++y)
        for (int x = 0; x < preview_.w; ++x) {
            const gfx::Color c = preview_.pixels[static_cast<std::size_t>(y) * preview_.w + x];
            if (gfx::a_of(c) == 0) continue;
            g.fill_rect_blend(preview_rect_.x + x * preview_scale_,
                              preview_rect_.y + y * preview_scale_,
                              preview_scale_, preview_scale_, c);
        }
    g.draw_rect(preview_rect_.x - 1, preview_rect_.y - 1, pw + 2, ph + 2, th::border_strong);

    if (sh != nullptr) {
        const auto it = sheets_.find(sh->name);
        for (int i = 0; i < tiles; ++i) {
            const ui::Rect r = part_rect(i);
            if (r.w == 0) continue;
            g.fill_rect(r.x, r.y, r.w, r.h, th::bg);
            if (it != sheets_.end()) {
                const gfx::Image& img = it->second;
                const int cols = img.w / sh->tile;
                const int sx = (i % cols) * sh->tile, sy = (i / cols) * sh->tile;
                for (int y = 0; y < sh->tile; ++y)
                    for (int x = 0; x < sh->tile; ++x) {
                        const gfx::Color c =
                            img.pixels[static_cast<std::size_t>(sy + y) * img.w + (sx + x)];
                        if (gfx::a_of(c) == 0) continue;
                        g.fill_rect_blend(r.x + x * strip_scale_, r.y + y * strip_scale_,
                                          strip_scale_, strip_scale_, c);
                    }
            }
            g.draw_rect(r.x, r.y, r.w, r.h, th::border_strong);
        }
    }
    g.pop_clip();
}

void MixWorkspace::draw_inspector(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) {
    g.fill_round_rect(area.x, area.y, area.w, area.h, th::radius_md, th::elevated);
    ui.push_id("mixi");
    controls_.clear();

    const int x = area.x + th::space_md;
    const int w = area.w - th::space_md * 2;
    int y = area.y + th::space_md;

    g.set_font_size(th::sz_label);
    g.draw_text(x, y, loaded_ ? mix_.name.c_str() : "Mixer", loaded_ ? th::text : th::text_muted);
    y += th::sz_label + th::space_xs;
    g.set_font_size(th::sz_caption);
    {
        char sub[96];
        std::snprintf(sub, sizeof sub, "%zu part(s)   %zu swap(s)", mix_.parts.size(),
                      mix_.swaps.size());
        g.draw_text(x, y, sub, th::text_muted);
    }
    y += th::sz_caption + th::space_md;

    g.set_font_size(th::sz_caption);
    g.draw_text(x, y, "PARTS   pick from the strip", th::text_muted);
    y += th::sz_caption + th::space_xs;
    for (std::size_t i = 0; i < mix_.parts.size() && i < 8; ++i) {
        char row[64];
        std::snprintf(row, sizeof row, "%zu.  %s %d", i + 1, mix_.parts[i].sheet.c_str(),
                      mix_.parts[i].index);
        ui.push_id(static_cast<int>(i));
        ui.list_item(ui::Rect{x, y, w, 22}, row, false);
        ui.pop_id();
        y += 22 + th::space_xs;
    }
    mark("remove", ui::Rect{x, y, w, 26});
    if (ui.button(ui::Rect{x, y, w, 26}, "Remove the top part", false, mix_.parts.size() > 1))
        want_remove_ = true;
    y += 26 + th::space_md;

    g.set_font_size(th::sz_caption);
    // Short enough to FIT. The first version of each of these ran off the panel edge,
    // which a screenshot showed and no assertion could: a caption clipped mid-word is
    // an instruction nobody can follow.
    g.draw_text(x, y, picked_ ? "COLOUR   click again to cycle"
                              : "COLOUR   click the sprite", th::text_muted);
    y += th::sz_caption + th::space_xs;
    {
        const ui::Rect sw{x, y, 26, 22};
        g.fill_round_rect(sw.x, sw.y, sw.w, sw.h, th::radius_sm, picked_ ? picked_ : th::bg);
        g.draw_round_rect(sw.x, sw.y, sw.w, sw.h, th::radius_sm, th::border_strong);
        char lbl[48];
        std::snprintf(lbl, sizeof lbl, "%zu swap(s)", mix_.swaps.size());
        g.set_font_size(th::sz_caption);
        g.draw_text(sw.x + sw.w + th::space_sm, y + 4, lbl, th::text_muted);
        y += 22 + th::space_md;
    }

    // ---- pinned to the bottom ----------------------------------------------
    {
        int by = area.y + area.h - th::space_md - (26 + th::space_xs) * 3 - th::sz_caption;
        const int half = w / 2 - 2;
        mark("undo", ui::Rect{x, by, half, 26});
        if (ui.button(ui::Rect{x, by, half, 26}, "Undo", false, stack_.can_undo())) want_undo_ = true;
        if (ui.button(ui::Rect{x + w / 2 + 2, by, half, 26}, "Redo", false, stack_.can_redo()))
            want_redo_ = true;
        by += 26 + th::space_xs;
        mark("next", ui::Rect{x, by, w, 26});
        if (ui.button(ui::Rect{x, by, w, 26}, "Next mix", false, paths_.size() > 1))
            want_next_ = true;
        by += 26 + th::space_xs;
        // Two buttons because they are two things. Save writes the SOURCE; Bake writes
        // the .hrt the game loads. One button would hide which of them is the artefact.
        mark("save", ui::Rect{x, by, half, 26});
        if (ui.button(ui::Rect{x, by, half, 26}, dirty() ? "Save *" : "Save", dirty(), loaded_))
            want_save_ = true;
        mark("bake", ui::Rect{x + w / 2 + 2, by, half, 26});
        if (ui.button(ui::Rect{x + w / 2 + 2, by, half, 26}, "Bake", false, loaded_))
            want_bake_ = true;
        by += 26 + th::space_xs;
        g.set_font_size(th::sz_caption);
        g.draw_text(x, by, stack_.can_undo() ? ("undo: " + stack_.undo_label()).c_str()
                                             : "nothing to undo", th::text_muted);
    }

    ui.pop_id();
}

} // namespace studioshell
