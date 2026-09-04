// =============================================================================
//  games/studio_shell/studio_shell_scene.cpp  —  nav rail + panels
// =============================================================================
#include "games/studio_shell/studio_shell_scene.hpp"

#include <cstdio>
#include <cstddef>
#include <string>
#include <utility>

#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/hub/hub_build.hpp"
#include "engine/project/inspect.hpp"
#include "engine/release/ops.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"
#include "engine/ui/ui_input.hpp"

namespace studioshell {

namespace th = ui::theme;

namespace {
const char* const kSections[] = {"Edit", "Project", "Play", "Hub", "Guide", "Learn", "About"};

// Toolbar buttons, in the order they are drawn. Indices are what the draw pass
// hands back to update(), because a click is discovered while drawing and acted
// on while updating — the same round trip the nav rail already makes.
enum PlayBtn { BtnPlay = 0, BtnPause, BtnStep, BtnStop };

// The Learn panel is a static map from the platform to its documentation — the roadmap's
// "build your first project" journey, pointing at the guide and the chapters behind it.
const char* const kLearnLines[] = {
    "Author to URL — the golden path in seven commands + a failure lab:",
    "   docs/guides/author-to-url.md",
    "",
    "The platform spine, chapter by chapter:",
    "   90  project manifest & golden path      93  immutable release store",
    "   91  resource identity & closure         94  atomic/audited releases",
    "   92  package manifest                    95  hub: next recommended action",
    "                                           96  graphical hub scene",
    "",
    "Try the loop: publish, then promote — watch the Hub tab update.",
};

// The Guide panel: what this build can actually be driven to do. Every mode `demo`
// dispatches, with the keys that make it move — so a new user does not have to read
// src/main.cpp to find the app. Static text on purpose: the dispatch table is small
// and hand-maintained, and a generated one would need main.cpp to grow an export seam.
// ponytail: keep it a flat list; it earns structure when the mode count outgrows a screen.
const char* const kGuideLines[] = {
    "Run any mode from the repo root:   ./build/demo <flag>",
    "",
    "WINDOWED",
    "  (no flag)|M0 engine demo — the 480×270 software framebuffer",
    "  --gui|chess [hvh|hvai] [easy|medium|hard] — click a piece, click a square",
    "  --fps|raycaster — W/S walk, A/D turn; plays maps/level_00.map",
    "  --3d|3D core — drag to orbit, WASD, ENTER/SPACE change shading",
    "  --viz3d|3D sandbox — 1–4 spawn, click select, drag to move",
    "  --iso|farm sim — 1–0 brush, LMB paint, RMB walk farmer, F5/F9 save/load",
    "  --editor|immediate-mode GUI + physics — click to drop bodies",
    "  --colony|colony sim on ECS + jobs (uses the BaaS when one is running)",
    "  --studio|Texture Lab — sliders, Save → .hrt, Export Sheet → sprites/",
    "  --sandbox|actors + behaviors — drag to place, Play/Stop, F5/F9",
    "  --maplab|level editor — paint tiles + Spawn, Save → maps/level_NN.map",
    "  --fx --light|particles / 2D lights   (--audio mixer, --anim flipbook)",
    "  --hub-ui|this shell's Hub tab, standalone",
    "",
    "HEADLESS (prints to the terminal, no window)",
    "  --project-inspect|validate a manifest + its resource closure",
    "  --project-publish|<proj> development \"why\"  — then promote from the Hub tab",
    "  --release-status|where development / preview / production point",
    "  --project-verify|<proj> <channel> — exit 0 = match, 2 = drift",
    "",
    "  --shell|this Studio: Map workspace + Hub + these pages",
    "",
    "THIS SHELL",
    "  Cmd+1..7|switch section (click the rail too)   Cmd+K = command palette",
    "  Edit|workspace tabs; Map: B/R/G tool, 0-9 brush, RMB erase, MMB pan, wheel zoom, Cmd+S/Cmd+Z",
    "  Project|declared assets + their hashes, and why the project is / is not shippable (R re-reads)",
    "  Play|run this project's entry scene in the Studio — Pause, Step one frame, Esc releases the keyboard",
    "  Hub|Space publish, 1 promote to preview, 2 to production, R refresh",
};

}  // namespace

std::string StudioShellScene::map_asset_of(const std::string& project_path,
                                           const std::vector<std::string>& known_entries) {
    // The fifth copy of "read and parse the manifest" used to live here. It reads
    // through engine::inspect now, so the workspace opens the map the browser lists.
    for (const engine::InspectedAsset& a : engine::inspect(project_path, known_entries).assets)
        if (a.type == "map" && a.present) return a.path;
    return {};
}

StudioShellScene::StudioShellScene(std::string project_path,
                                   std::vector<std::string> known_entries)
    : project_path_(std::move(project_path)),
      known_entries_(std::move(known_entries)),
      map_(map_asset_of(project_path_, known_entries_)) {
    rebuild();
    workspaces_ = {&map_};
    // Each workspace binds its own ids here, so the palette lists exactly what THIS
    // process can do. --cmd in a terminal sees the release commands and not these,
    // which is the truth: there is no document open in a terminal.
    for (Workspace* w : workspaces_) {
        w->register_commands();
        // First one wins: the screen has one modal, and asking about two recoveries
        // at once would stack two cards on top of each other.
        if (!recovering_ && w->recovery_pending()) { recovering_ = w; modal_ = Modal::Recovery; }
    }
}

void StudioShellScene::flash(const engine::OpResult& r, double seconds) {
    flash_ = r.message; flash_ok_ = r.ok; flash_t_ = seconds;
}

void StudioShellScene::set_clipboard(std::function<std::string()> get,
                                     std::function<void(const std::string&)> set) {
    clip_get_ = std::move(get);
    clip_set_ = std::move(set);
}

void StudioShellScene::rebuild() {
    hub_        = engine::build_hub_view(project_path_, known_entries_);
    inspection_ = engine::inspect(project_path_, known_entries_);
    history_    = engine::log();
}

void StudioShellScene::run(projectui::Op op) {
    switch (op) {
        case projectui::Op::Reinspect:
            rebuild();
            flash(engine::OpResult{true, "re-read " + project_path_ + " and re-hashed " +
                                         std::to_string(inspection_.assets.size()) + " asset(s)"},
                  3.0);
            break;
        case projectui::Op::CopyPackageHash:
            if (!inspection_.package.empty() && clip_set_) {
                clip_set_(inspection_.package);
                flash(engine::OpResult{true, "copied " + inspection_.package}, 3.0);
            }
            break;
        case projectui::Op::None: break;
    }
}

void StudioShellScene::run(hubui::Op op) {
    auto did = [&](const engine::OpResult& r) {
        flash_ = r.message; flash_ok_ = r.ok; flash_t_ = 5.0; rebuild();
    };
    const std::string why = reason_.empty() ? std::string("shell") : reason_;
    switch (op) {
        case hubui::Op::Publish:           did(engine::publish(project_path_, "development", why, known_entries_)); break;
        case hubui::Op::PromotePreview:    did(engine::promote("development", "preview", why)); break;
        case hubui::Op::PromoteProduction: did(engine::promote("preview", "production", why)); break;
        case hubui::Op::Refresh:           rebuild(); break;
        case hubui::Op::CopySourceHash:
            if (hub_ && !hub_->local_package.empty() && clip_set_) {
                clip_set_(hub_->local_package);
                flash_ = "copied " + hub_->local_package; flash_ok_ = true; flash_t_ = 3.0;
            }
            break;
        case hubui::Op::None: break;
    }
}

void StudioShellScene::update(double dt, const platform::InputState& in) {
    if (flash_t_ > 0) flash_t_ -= dt;

    if (nav_click_ >= 0) { section_ = nav_click_; nav_click_ = -1; }

#ifdef __APPLE__
    const bool cmd = in.mods.super;
#else
    const bool cmd = in.mods.ctrl;
#endif

    // The rail moved off the arrow keys: a workspace needs them, and a key that means
    // two things means neither. Cmd+1..5 is what every tabbed application already uses.
    if (modal_ == Modal::None && cmd && !palette_.is_open()) {
        for (int i = 0; i < SectionCount; ++i) {
            const auto k = static_cast<platform::Key>(static_cast<int>(platform::Key::Num1) + i);
            if (in.pressed(k)) section_ = i;
        }
    }

    // ---- command palette ----
    if (cmd && in.pressed(platform::Key::K) && modal_ == Modal::None) {
        if (palette_.is_open()) palette_.close(); else palette_.open();
    }
    if (palette_.is_open()) {
        std::string chosen = palette_click_;
        palette_click_.clear();
        if (in.pressed(platform::Key::Escape)) palette_.close();
        if (in.repeated(platform::Key::Down))  palette_.move(1);
        if (in.repeated(platform::Key::Up))    palette_.move(-1);
        if (in.pressed(platform::Key::Enter))  chosen = palette_.selected();
        if (!chosen.empty()) {
            // A command that needs arguments is listed so you know it exists, but a
            // one-line box is not where three validated values get typed — the Hub
            // tab's dialog is. Say what it wants instead of running it wrong.
            const cmd::Info* info = nullptr;
            for (const cmd::Info& i : cmd::all()) if (i.id == chosen) info = &i;
            if (info && !info->args_help.empty()) {
                flash(engine::OpResult{false, chosen + " needs arguments: " + info->args_help});
            } else {
                flash(cmd::run(chosen));
                rebuild();
            }
            palette_.close();
        }
        // The palette owns the keyboard while it is up; nothing below sees this frame.
        for (Workspace* w : workspaces_) w->update(dt, in, /*interactive*/ false);
        play_.update(dt, in, /*focused*/ false);   // keeps running, receives nothing
        return;
    }

    // ---- the map workspace ----
    // Every workspace updates every frame; only the visible one is interactive. A
    // workspace that stopped ticking when you switched tabs would lose its autosave
    // timer, which is exactly when an autosave matters most.
    if (ws_click_ >= 0) { ws_ = ws_click_; ws_click_ = -1; }
    for (std::size_t i = 0; i < workspaces_.size(); ++i) {
        const bool live = modal_ == Modal::None && section_ == Edit &&
                          static_cast<int>(i) == ws_;
        workspaces_[i]->update(dt, in, live);
        if (auto msg = workspaces_[i]->take_message()) flash(*msg);
    }

    // ---- the play viewport --------------------------------------------------
    // Focus is claimed by clicking the frame and released with Escape. Without an
    // explicit release the keyboard would be trapped: the game would eat every key
    // that is not a Studio chord, including the ones that would get you out.
    if (play_focus_click_) { play_focused_ = true; play_focus_click_ = false; }
    if (section_ != Play || !play_.running()) play_focused_ = false;
    if (play_focused_ && in.pressed(platform::Key::Escape)) play_focused_ = false;

    const int btn = play_button_;
    play_button_ = -1;
    if (btn == BtnPlay) {
        // Restart is stop-then-start, so a restarted game gets a fresh scene AND a
        // fresh clock rather than the previous run's leftovers.
        play_.stop();
        flash(play_.start(inspection_.project.entry), 3.0);
        play_focused_ = play_.running();
    } else if (btn == BtnPause) {
        play_.set_paused(!play_.paused());
    } else if (btn == BtnStep) {
        play_.step_once();
    } else if (btn == BtnStop) {
        play_.stop();
        play_focused_ = false;
    }
    // The scene advances on the shell's own fixed step. Input reaches it only when
    // the Play section is showing AND the frame has focus.
    play_.update(dt, in, section_ == Play && play_focused_ && modal_ == Modal::None);

    // R re-reads from disk: the panel is a snapshot, and the files it describes are
    // edited by other tools while the Studio is open.
    if (section_ == Project && modal_ == Modal::None && !cmd && in.pressed(platform::Key::R))
        run(projectui::Op::Reinspect);

    // ---- hub operations ----
    hubui::Op asked = requested_;
    requested_ = hubui::Op::None;

    if (section_ == Hub && modal_ == Modal::None && asked == hubui::Op::None && !cmd) {
        if      (in.pressed(platform::Key::Space)) asked = hubui::Op::Publish;
        else if (in.pressed(platform::Key::Num1))  asked = hubui::Op::PromotePreview;
        else if (in.pressed(platform::Key::Num2))  asked = hubui::Op::PromoteProduction;
        else if (in.pressed(platform::Key::R))     asked = hubui::Op::Refresh;
    }

    // Refresh and copy change nothing a channel points at, so they run immediately.
    // Everything else appends to the audit log and needs a typed reason first.
    if (asked == hubui::Op::Refresh || asked == hubui::Op::CopySourceHash) {
        run(asked);
    } else if (asked != hubui::Op::None && modal_ == Modal::None) {
        confirming_ = asked;
        modal_ = Modal::HubOp;
        reason_.clear();
    }
}

// The Map section: canvas on the left, inspector on the right, status strip under
// both. The split is fixed — a draggable one needs a cursor shape, a hit zone and a
// persisted position, and no second author has asked for it yet.
void StudioShellScene::draw_edit_section(gfx::Renderer2D& g, ui::Rect area) {
    Workspace& ws = *workspaces_[static_cast<std::size_t>(ws_)];

    // ---- the tab row: one per open workspace -------------------------------
    // A dirty document is marked in the TAB, not only in the status strip. The strip
    // describes the workspace you are looking at; the tab is the only place the other
    // one can tell you it has unsaved work.
    {
        std::vector<std::string> labels;
        std::vector<const char*> ptrs;
        labels.reserve(workspaces_.size());
        for (const auto& w : workspaces_)
            labels.push_back(std::string(w->name()) + (w->dirty() ? " *" : ""));
        for (const auto& l : labels) ptrs.push_back(l.c_str());
        const int chosen = ui_.tabs("wstabs", ui::Rect{area.x, area.y, area.w, 30},
                                    ptrs.data(), static_cast<int>(ptrs.size()), ws_);
        if (chosen != ws_) ws_click_ = chosen;
    }
    const int tabs_h = 30 + th::space_md;

    // The workspace asks for an inspector width; the shell decides. A narrow window
    // must not leave a canvas thinner than the panel beside it.
    int insp_w = ws.inspector_width();
    if (insp_w > area.w / 2) insp_w = area.w / 2;

    const int status_h = th::sz_caption + th::space_md;
    const ui::Rect body{area.x, area.y + tabs_h, area.w,
                        area.h - tabs_h - status_h - th::space_sm};

    ws.draw_canvas(ui_, g, ui::Rect{body.x, body.y,
                                    body.w - insp_w - th::space_md, body.h});
    ws.draw_inspector(ui_, g, ui::Rect{body.x + body.w - insp_w, body.y, insp_w, body.h});

    g.set_font_size(th::sz_caption);
    const int sy = area.y + area.h - th::sz_caption;
    const std::string left = ws.status();
    g.draw_text(area.x, sy, left.c_str(), ws.dirty() ? th::warn : th::text_muted);
    const char* hint = ws.hint();
    g.draw_text(area.x + area.w - g.text_width(hint), sy, hint, th::text_muted);
}

// The Play section: a toolbar, the game letterboxed under it, and a status line that
// says what is actually true — how many fixed steps have run, and whether the keyboard
// is going to the game or to the Studio.
void StudioShellScene::draw_play_section(gfx::Renderer2D& g, ui::Rect area,
                                         text::Font* font, double dt) {
    int y = area.y;
    g.set_font_size(th::sz_title);
    g.draw_text(area.x, y, "Play", th::text);
    {
        // State as a WORD, not only as a button that looks pressed. "Paused" and
        // "running with nothing moving on screen" are indistinguishable otherwise.
        const char* word = !play_.has_factory() ? "unavailable"
                           : !play_.running()   ? "stopped"
                           : play_.paused()     ? "paused" : "running";
        const ui::Tone tone = !play_.running()  ? ui::Tone::Neutral
                              : play_.paused()  ? ui::Tone::Warning
                                                : ui::Tone::Success;
        g.set_font_size(th::sz_caption);
        ui_.badge(area.x + area.w - g.text_width(word) - th::space_lg, y + th::space_xs,
                  word, tone);
    }
    y += th::sz_title + th::space_sm;

    // ---- toolbar ------------------------------------------------------------
    {
        ui_.push_id("playbar");
        ui_.begin_layout(ui::Rect{area.x, y, area.w, 30}, ui::Axis::X,
                         ui::LayoutOpts{th::space_sm, 0});
        const bool can = play_.has_factory() && inspection_.parsed &&
                         !inspection_.project.entry.empty();
        if (ui_.button(ui_.slot(110), play_.running() ? "Restart" : "Play",
                       /*primary*/ !play_.running(), can))
            play_button_ = BtnPlay;
        if (ui_.button(ui_.slot(100), play_.paused() ? "Resume" : "Pause", false, play_.running()))
            play_button_ = BtnPause;
        // Step is only meaningful while paused: it exists to advance one fixed step
        // and look at it, which is the whole reason to embed a player rather than
        // launch the game.
        if (ui_.button(ui_.slot(90), "Step", false, play_.running() && play_.paused()))
            play_button_ = BtnStep;
        if (ui_.button(ui_.slot(90), "Stop", false, play_.running()))
            play_button_ = BtnStop;
        ui_.end_layout();
        ui_.pop_id();
    }
    y += 30 + th::space_md;

    const int status_h = th::sz_caption + th::space_md;
    const ui::Rect view{area.x, y, area.w, area.y + area.h - y - status_h};

    if (!play_.running()) {
        g.fill_round_rect(view.x, view.y, view.w, view.h, th::radius_md, th::elevated);
        g.set_font_size(th::sz_body);
        const char* why =
            !play_.has_factory()
                ? "This build cannot play: no scene factory was wired in."
            : !inspection_.parsed
                ? "No project to play — the manifest could not be read."
            : inspection_.project.entry.empty()
                ? "The manifest declares no entry."
                : "Press Play to run this project's entry scene here.";
        g.draw_text(view.x + th::space_lg, view.y + th::space_lg, why, th::text_muted);
        if (play_.has_factory() && inspection_.parsed && !inspection_.project.entry.empty()) {
            g.set_font_size(th::sz_caption);
            g.draw_text(view.x + th::space_lg, view.y + th::space_lg + th::sz_body + th::space_sm,
                        ("entry  " + inspection_.project.entry).c_str(), th::text_dim);
        }
    } else {
        const ui::Rect shown = play_.draw(g, view, font, dt);
        // The click that gives the game the keyboard. hit() draws nothing — the frame
        // underneath IS the control — so this is the whole focus affordance.
        bool hovered = false;
        if (ui_.hit("playview", shown, &hovered)) play_focus_click_ = true;
        if (play_focused_)
            g.draw_rect(shown.x - 2, shown.y - 2, shown.w + 4, shown.h + 4, th::accent);
        else if (hovered)
            g.draw_rect(shown.x - 2, shown.y - 2, shown.w + 4, shown.h + 4, th::border_strong);
    }

    // ---- status -------------------------------------------------------------
    g.set_font_size(th::sz_caption);
    const int sy = area.y + area.h - th::sz_caption;
    std::string left;
    if (play_.running()) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s   %dx%d   t=%.2fs   %lld steps",
                      play_.entry().c_str(), play_.width(), play_.height(),
                      play_.clock(), play_.steps());
        left = buf;
    } else {
        left = "stopped";
    }
    g.draw_text(area.x, sy, left.c_str(), th::text_muted);
    const char* hint = play_.running()
                           ? (play_focused_ ? "the game has the keyboard  ·  Esc returns it to the Studio"
                                            : "click the frame to give the game the keyboard")
                           : "Cmd+K commands   ·   the game runs at its own native size";
    g.draw_text(area.x + area.w - g.text_width(hint), sy, hint,
                play_focused_ ? th::accent : th::text_muted);
}

void StudioShellScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const int w = g.width(), h = g.height();
    g.clear(th::bg);
    g.set_font(ctx.font, th::sz_body);

    ui_.set_clipboard(clip_get_, clip_set_);
    ui_.begin(&g, ui::from_platform(ctx.input));

    // Everything behind an open overlay is drawn but inert — that is what makes it
    // modal rather than merely on top.
    if (modal_ != Modal::None || palette_.is_open()) ui_.begin_inert();

    // ---- left nav rail ----
    const int rail = 200;
    g.fill_rect(0, 0, rail, h, th::elevated);
    g.fill_rect(rail - 1, 0, 1, h, th::border);
    g.set_font_size(th::sz_title);
    g.draw_text(th::space_lg, th::space_xl, "Studio", th::text);

    ui_.push_id("nav");
    ui_.begin_layout(ui::Rect{th::space_md, th::space_xl + th::sz_title + th::space_xl,
                              rail - th::space_md * 2, h},
                     ui::Axis::Y, ui::LayoutOpts{th::space_xs, 0});
    for (int i = 0; i < SectionCount; ++i)
        if (ui_.button(ui_.slot(32), kSections[i], /*primary*/ i == section_)) nav_click_ = i;
    ui_.end_layout();
    ui_.pop_id();

    g.set_font_size(th::sz_caption);
    const int hint_line = th::sz_caption + th::space_xs;
    g.draw_text(th::space_lg, h - th::space_lg - hint_line * 2, "Cmd+1..7  switch section",
                th::text_muted);
    g.draw_text(th::space_lg, h - th::space_lg - hint_line, "Cmd+K  command palette",
                th::text_muted);

    // ---- main panel ----
    const ui::Rect area{rail + th::space_xl, th::space_xl,
                        w - rail - th::space_xl * 2, h - th::space_xl * 2};
    int y = area.y;

    if (section_ == Edit) {
        draw_edit_section(g, area);
    } else if (section_ == Project) {
        run(projectui::draw_project_panel(ui_, g, inspection_, area, asset_sel_));
    } else if (section_ == Play) {
        draw_play_section(g, area, ctx.font, ctx.dt);
    } else if (section_ == Hub) {
        const hubui::Op clicked = hubui::draw_hub_panel(ui_, g, hub_ ? &*hub_ : nullptr,
                                                        project_path_, area, history_);
        if (clicked != hubui::Op::None) requested_ = clicked;
    } else if (section_ == Guide) {
        g.set_font_size(th::sz_title);
        g.draw_text(area.x, y, "Guide", th::text);
        y += th::sz_title + th::space_lg;
        g.set_font_size(th::sz_body);
        for (const char* l : kGuideLines) {
            // "flag|description" draws as two columns — the UI face is proportional,
            // so padding with spaces would never line up. No '|' = a section header.
            const std::string s(l);
            const auto bar = s.find('|');
            if (bar == std::string::npos) {
                g.draw_text(area.x, y, l, th::accent);
            } else {
                g.draw_text(area.x, y, s.substr(0, bar).c_str(), th::warn);
                g.draw_text(area.x + 150, y, s.substr(bar + 1).c_str(), th::text_dim);
            }
            y += th::sz_body + th::space_xs;
        }
    } else if (section_ == Learn) {
        g.set_font_size(th::sz_title);
        g.draw_text(area.x, y, "Learn", th::text);
        y += th::sz_title + th::space_lg;
        g.set_font_size(th::sz_body);
        for (const char* l : kLearnLines) {
            g.draw_text(area.x, y, l, th::text_dim);
            y += th::sz_body + th::space_sm;
        }
    } else {  // About
        g.set_font_size(th::sz_title);
        g.draw_text(area.x, y, "About", th::text);
        y += th::sz_title + th::space_lg;
        g.set_font_size(th::sz_body);
        g.draw_text(area.x, y, "hand-engine — a transparent, self-hostable game-creation platform.",
                    th::text_dim);
        y += th::sz_body + th::space_md;
        g.draw_text(area.x, y, ("project: " + project_path_).c_str(), th::text_muted);
        y += th::sz_body + th::space_sm;
        if (hub_) g.draw_text(area.x, y, (hub_->name + "  (entry " + hub_->entry + ")").c_str(),
                              th::text_muted);
    }

    // ---- overlays: at most one, and it owns the keyboard ----
    if (modal_ == Modal::HubOp) {
        const ui::Confirm c = ui_.confirm("hubop", hubui::op_title(confirming_),
                                          hubui::op_body(confirming_),
                                          hubui::op_verb(confirming_),
                                          hubui::op_is_destructive(confirming_), &reason_);
        if (c == ui::Confirm::Yes) {
            run(confirming_);
            confirming_ = hubui::Op::None; modal_ = Modal::None;
        } else if (c == ui::Confirm::No) {
            confirming_ = hubui::Op::None; modal_ = Modal::None; reason_.clear();
        }
    } else if (modal_ == Modal::Recovery) {
        const ui::Confirm c = ui_.confirm(
            "recover", "Unsaved changes were found",
            ("An autosave of " + (recovering_ ? recovering_->path() : std::string()) +
             " is newer than the file.").c_str(),
            "Recover", /*danger*/ false);
        // Cancel keeps the saved file AND leaves the autosave alone: declining by
        // reflex must not be the thing that destroys the work.
        if (c == ui::Confirm::Yes && recovering_) {
            recovering_->take_recovery(); modal_ = Modal::None;
        } else if (c == ui::Confirm::No && recovering_) {
            recovering_->dismiss_recovery(); modal_ = Modal::None;
        }
        if (recovering_) if (auto msg = recovering_->take_message()) flash(*msg);
        if (modal_ == Modal::None) recovering_ = nullptr;
    } else if (palette_.is_open()) {
        const std::string clicked = palette_.draw(ui_, g);
        if (!clicked.empty()) palette_click_ = clicked;
    }

    if (flash_t_ > 0 && !flash_.empty())
        ui_.toast(flash_.c_str(), flash_ok_ ? ui::Tone::Success : ui::Tone::Danger);

    ui_.end();
}

} // namespace studioshell
