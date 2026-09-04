// =============================================================================
//  games/studio_shell/studio_shell_scene.hpp  —  the Studio shell (--shell)
// =============================================================================
//  Horizon 1's "shared Studio shell and dock/navigation model": a window with a left
//  nav rail (Hub / Guide / Learn / About) and a main panel that renders the selected
//  section. The Hub section is not a second implementation of the hub — it draws the
//  same hubui::draw_hub_panel that --hub-ui draws, over the same engine::hub_lines
//  content and the same engine::release ops that --hub prints and the CLI runs.
//
//  This scene is the frame and the navigation; every panel's substance lives in a
//  tested core, and every colour comes from ui::theme.
//  ponytail: four sections and the shell that hosts them. A dock manager, thumbnails,
//  and folding the Labs in come when a second author actually needs them.
// =============================================================================
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "engine/hub/hub.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "games/hub/hub_panel.hpp"
#include "games/studio_shell/map_workspace.hpp"
#include "games/studio_shell/palette.hpp"

namespace studioshell {

class StudioShellScene : public engine::Scene {
public:
    explicit StudioShellScene(std::string project_path);
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

    // Clipboard access is INJECTED rather than called. The scene must stay linkable
    // without the SDL backend — that is what lets test_shell_golden drive the whole
    // shell headless — so main.cpp wires these to platform::clipboard_* and a test
    // leaves them unset (copying then does nothing, which is the truth).
    void set_clipboard(std::function<std::string()> get,
                       std::function<void(const std::string&)> set);

    // The map the workspace opened, for tests and for the status bar.
    [[nodiscard]] const MapWorkspace& map_workspace() const { return map_; }
    [[nodiscard]] MapWorkspace&       map_workspace() { return map_; }

private:
    // Map first: this is an authoring tool, and the thing you came to do should be
    // the thing that is already open.
    enum Section { Map = 0, Hub, Guide, Learn, About, SectionCount };
    // One modal at a time, by construction. Two booleans would eventually both be
    // true and draw two cards on top of each other.
    enum class Modal { None, HubOp, Recovery };

    // The first `asset map` the manifest declares, or empty. Static so it can run in
    // the member-init list, before the object exists.
    static std::string map_asset_of(const std::string& project_path);

    void rebuild_hub();
    void run(hubui::Op op);
    void flash(const engine::OpResult& r, double seconds = 5.0);
    void draw_map_section(gfx::Renderer2D& g, ui::Rect area);

    std::string                    project_path_;
    std::vector<std::string>       known_entries_;
    int                            section_ = Map;   // an authoring tool opens on the work
    std::optional<engine::HubView> hub_;
    std::string                    flash_;
    bool                           flash_ok_ = true;
    double                         flash_t_ = 0;
    ui::Context                    ui_;
    hubui::Op                      requested_  = hubui::Op::None;   // asked this frame
    hubui::Op                      confirming_ = hubui::Op::None;   // pending hub op
    Modal                          modal_ = Modal::None;
    std::string                    reason_;
    int                            nav_click_ = -1;
    MapWorkspace                   map_;
    CommandPalette                 palette_;
    std::string                    palette_click_;   // id clicked during the last draw
    std::function<std::string()>            clip_get_;
    std::function<void(const std::string&)> clip_set_;
};

} // namespace studioshell
