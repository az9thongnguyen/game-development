// =============================================================================
//  games/studio_shell/studio_shell_scene.cpp  —  nav rail + panels
// =============================================================================
#include "games/studio_shell/studio_shell_scene.hpp"

#include <string>
#include <utility>

#include "engine/hub/hub_build.hpp"
#include "engine/release/ops.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"
#include "engine/ui/ui_input.hpp"

namespace studioshell {

namespace th = ui::theme;

namespace {
const char* const kSections[] = {"Hub", "Guide", "Learn", "About"};

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
    "This shell: click the rail, or Up/Down (or Tab). The Hub tab drives the releases.",
};

}  // namespace

StudioShellScene::StudioShellScene(std::string project_path)
    : project_path_(std::move(project_path)), known_entries_{"fps"} {
    rebuild_hub();
}

void StudioShellScene::set_clipboard(std::function<std::string()> get,
                                     std::function<void(const std::string&)> set) {
    clip_get_ = std::move(get);
    clip_set_ = std::move(set);
}

void StudioShellScene::rebuild_hub() { hub_ = engine::build_hub_view(project_path_, known_entries_); }

void StudioShellScene::run(hubui::Op op) {
    auto did = [&](const engine::OpResult& r) {
        flash_ = r.message; flash_ok_ = r.ok; flash_t_ = 5.0; rebuild_hub();
    };
    const std::string why = reason_.empty() ? std::string("shell") : reason_;
    switch (op) {
        case hubui::Op::Publish:           did(engine::publish(project_path_, "development", why, known_entries_)); break;
        case hubui::Op::PromotePreview:    did(engine::promote("development", "preview", why)); break;
        case hubui::Op::PromoteProduction: did(engine::promote("preview", "production", why)); break;
        case hubui::Op::Refresh:           rebuild_hub(); break;
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

    // Tab now belongs to the UI (it moves focus between controls), so the nav rail
    // is driven by the arrow keys alone. A key that means two things means neither.
    if (confirming_ == hubui::Op::None) {
        if (in.pressed(platform::Key::Down)) section_ = (section_ + 1) % SectionCount;
        if (in.pressed(platform::Key::Up))   section_ = (section_ + SectionCount - 1) % SectionCount;
    }

    // A click resolves during render (that is where the layout is known) and arrives
    // here next frame; keys are read directly. Both funnel into one place, so mouse
    // and keyboard cannot become two different ways of publishing.
    hubui::Op asked = requested_;
    requested_ = hubui::Op::None;

    if (section_ == Hub && confirming_ == hubui::Op::None && asked == hubui::Op::None) {
        if      (in.pressed(platform::Key::Space)) asked = hubui::Op::Publish;
        else if (in.pressed(platform::Key::Num1))  asked = hubui::Op::PromotePreview;
        else if (in.pressed(platform::Key::Num2))  asked = hubui::Op::PromoteProduction;
        else if (in.pressed(platform::Key::R))     asked = hubui::Op::Refresh;
    }

    // Refresh and copy change nothing a channel points at, so they run immediately.
    // Everything else appends to the audit log and needs a typed reason first.
    if (asked == hubui::Op::Refresh || asked == hubui::Op::CopySourceHash) {
        run(asked);
    } else if (asked != hubui::Op::None && confirming_ == hubui::Op::None) {
        confirming_ = asked;
        reason_.clear();
    }
}

void StudioShellScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const int w = g.width(), h = g.height();
    g.clear(th::bg);
    g.set_font(ctx.font, th::sz_body);

    ui_.set_clipboard(clip_get_, clip_set_);
    ui_.begin(&g, ui::from_platform(ctx.input));

    // Everything behind an open dialog is drawn but inert — that is what makes it
    // modal rather than merely on top.
    if (confirming_ != hubui::Op::None) ui_.begin_inert();

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
    g.draw_text(th::space_lg, h - th::space_xl, "Up / Down to switch", th::text_muted);

    // ---- main panel ----
    const ui::Rect area{rail + th::space_xl, th::space_xl,
                        w - rail - th::space_xl * 2, h - th::space_xl * 2};
    int y = area.y;

    if (section_ == Hub) {
        const hubui::Op clicked = hubui::draw_hub_panel(ui_, g, hub_ ? &*hub_ : nullptr,
                                                        project_path_, area);
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

    if (confirming_ != hubui::Op::None) {
        const ui::Confirm c = ui_.confirm("hubop", hubui::op_title(confirming_),
                                          hubui::op_body(confirming_),
                                          hubui::op_verb(confirming_),
                                          hubui::op_is_destructive(confirming_), &reason_);
        if      (c == ui::Confirm::Yes) { run(confirming_); confirming_ = hubui::Op::None; }
        else if (c == ui::Confirm::No)  { confirming_ = hubui::Op::None; reason_.clear(); }
    }

    if (flash_t_ > 0 && !flash_.empty())
        ui_.toast(flash_.c_str(), flash_ok_ ? ui::Tone::Success : ui::Tone::Danger);

    ui_.end();
}

} // namespace studioshell
