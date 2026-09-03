// =============================================================================
//  games/studio_shell/studio_shell_scene.cpp  —  nav rail + panels
// =============================================================================
#include "games/studio_shell/studio_shell_scene.hpp"

#include <string>
#include <utility>

#include "engine/color.hpp"
#include "engine/hub/hub_build.hpp"
#include "engine/release/ops.hpp"

namespace studioshell {

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
    "   92  package manifest                     95  hub: next recommended action",
    "                                            96  graphical hub scene",
    "",
    "Try the loop:  Space publishes, 1/2 promote — watch the Hub tab update.",
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
    "  (no flag)|M0 engine demo - the 480x270 software framebuffer",
    "  --gui|chess [hvh|hvai] [easy|medium|hard] - click a piece, click a square",
    "  --fps|raycaster - W/S walk, A/D turn; plays maps/level_00.map",
    "  --3d|3D core - drag to orbit, WASD, ENTER/SPACE change shading",
    "  --viz3d|3D sandbox - 1-4 spawn, click select, drag to move",
    "  --iso|farm sim - 1-0 brush, LMB paint, RMB walk farmer, F5/F9 save/load",
    "  --editor|immediate-mode GUI + physics - click to drop bodies",
    "  --colony|colony sim on ECS + jobs (uses the BaaS when one is running)",
    "  --studio|Texture Lab - sliders, Save -> .hrt, Export Sheet -> sprites/",
    "  --sandbox|actors + behaviors - drag to place, Play/Stop, F5/F9",
    "  --maplab|level editor - paint tiles + Spawn, Save -> maps/level_NN.map",
    "  --fx --light|particles / 2D lights   (--audio mixer, --anim flipbook)",
    "  --hub-ui|this shell's Hub tab, standalone",
    "",
    "HEADLESS (prints to the terminal, no window)",
    "  --project-inspect|validate a manifest + its resource closure",
    "  --project-publish|<proj> development \"why\"  - then 1/2 here to promote",
    "  --release-status|where development / preview / production point",
    "  --project-verify|<proj> <channel> - exit 0 = match, 2 = drift",
    "",
    "This shell: Up/Down (or Tab) switches tab. The Hub tab drives the releases.",
};

}  // namespace

StudioShellScene::StudioShellScene(std::string project_path)
    : project_path_(std::move(project_path)), known_entries_{"fps"} {
    rebuild_hub();
}

void StudioShellScene::rebuild_hub() { hub_ = engine::build_hub_view(project_path_, known_entries_); }

void StudioShellScene::update(double dt, const platform::InputState& in) {
    if (flash_t_ > 0) flash_t_ -= dt;

    // Navigation: Up/Down (or Tab) cycles the section.
    if (in.pressed(platform::Key::Down) || in.pressed(platform::Key::Tab))
        section_ = (section_ + 1) % SectionCount;
    if (in.pressed(platform::Key::Up))
        section_ = (section_ + SectionCount - 1) % SectionCount;

    // The Hub section is the interactive controller — the same ops as --hub-ui.
    if (section_ == Hub) {
        auto did = [&](const engine::OpResult& r) { flash_ = r.message; flash_t_ = 5.0; rebuild_hub(); };
        if      (in.pressed(platform::Key::Space)) did(engine::publish(project_path_, "development", "shell", known_entries_));
        else if (in.pressed(platform::Key::Num1))  did(engine::promote("development", "preview", "shell"));
        else if (in.pressed(platform::Key::Num2))  did(engine::promote("preview", "production", "shell"));
        else if (in.pressed(platform::Key::R))     rebuild_hub();
    }
}

void StudioShellScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    g.clear(gfx::rgb(12, 14, 20));

    // ---- left nav rail ----
    const int rail = 180;
    g.fill_rect(0, 0, rail, h_, gfx::rgb(20, 23, 32));
    g.set_font(ctx.font, 20);
    g.draw_text(20, 40, "Studio", gfx::rgb(255, 255, 255));
    g.set_font_size(18);
    for (int i = 0; i < SectionCount; ++i) {
        const bool sel = (i == section_);
        if (sel) g.fill_rect(0, 70 + i * 40 - 20, rail, 36, gfx::rgb(38, 44, 60));
        g.draw_text(20, 70 + i * 40, kSections[i], sel ? gfx::rgb(255, 255, 255) : gfx::rgb(150, 158, 172));
    }
    g.set_font_size(13);
    g.draw_text(16, h_ - 20, "Up/Down: switch", gfx::rgb(96, 104, 118));

    // ---- main panel ----
    const int px = rail + 28;
    int y = 44;
    if (section_ == Hub) {
        if (!hub_) {
            g.set_font_size(18);
            g.draw_text(px, y, ("cannot read '" + project_path_ + "'").c_str(), gfx::rgb(255, 120, 120));
            return;
        }
        const auto lines = engine::hub_lines(*hub_);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::string& s = lines[i];
            gfx::Color c = gfx::rgb(210, 216, 228);
            if (i == 0) { g.set_font_size(24); c = gfx::rgb(255, 255, 255); }
            else        { g.set_font_size(18); }
            if (s.rfind("next:", 0) == 0)                          c = gfx::rgb(120, 220, 140);
            else if (s.rfind("  - ", 0) == 0)                      c = gfx::rgb(255, 180, 90);
            else if (s.find("NOT shippable") != std::string::npos) c = gfx::rgb(255, 120, 120);
            g.draw_text(px, y, s.c_str(), c);
            y += (i == 0) ? 40 : 26;
        }
        if (flash_t_ > 0 && !flash_.empty()) {
            g.set_font_size(16);
            g.draw_text(px, h_ - 52, flash_.c_str(), gfx::rgb(120, 220, 140));
        }
        g.set_font_size(14);
        g.draw_text(px, h_ - 26, "Space: publish→dev    1: promote→preview    2: promote→production    R: refresh",
                    gfx::rgb(120, 128, 140));
    } else if (section_ == Guide) {
        g.set_font_size(24); g.draw_text(px, y, "Guide", gfx::rgb(255, 255, 255)); y += 40;
        g.set_font_size(14);
        for (const char* l : kGuideLines) {
            // "flag|description" draws as two columns — the shared font is proportional,
            // so padding with spaces would never line up. No '|' = a section header.
            const std::string s(l);
            const auto bar = s.find('|');
            if (bar == std::string::npos) {
                g.draw_text(px, y, l, gfx::rgb(150, 200, 255));
            } else {
                g.draw_text(px, y, s.substr(0, bar).c_str(), gfx::rgb(255, 210, 130));
                g.draw_text(px + 130, y, s.substr(bar + 1).c_str(), gfx::rgb(200, 206, 220));
            }
            y += 19;
        }
    } else if (section_ == Learn) {
        g.set_font_size(24); g.draw_text(px, y, "Learn", gfx::rgb(255, 255, 255)); y += 44;
        g.set_font_size(16);
        for (const char* l : kLearnLines) { g.draw_text(px, y, l, gfx::rgb(200, 206, 220)); y += 26; }
    } else {  // About
        g.set_font_size(24); g.draw_text(px, y, "About", gfx::rgb(255, 255, 255)); y += 44;
        g.set_font_size(18);
        g.draw_text(px, y, "hand-engine — a transparent, self-hostable game-creation platform.", gfx::rgb(200, 206, 220)); y += 30;
        g.draw_text(px, y, ("project: " + project_path_).c_str(), gfx::rgb(160, 168, 182)); y += 26;
        if (hub_) g.draw_text(px, y, (hub_->name + "  (entry " + hub_->entry + ")").c_str(), gfx::rgb(160, 168, 182));
    }
}

} // namespace studioshell
