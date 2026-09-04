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
#include <optional>
#include <string>
#include <vector>

#include "engine/hub/hub.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "games/hub/hub_panel.hpp"

namespace studioshell {

class StudioShellScene : public engine::Scene {
public:
    explicit StudioShellScene(std::string project_path);
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    enum Section { Hub = 0, Guide, Learn, About, SectionCount };

    void rebuild_hub();
    void run(const hubui::Action& a);

    std::string                    project_path_;
    std::vector<std::string>       known_entries_;
    int                            section_ = Hub;
    std::optional<engine::HubView> hub_;
    std::string                    flash_;
    double                         flash_t_ = 0;
    ui::Context                    ui_;
    hubui::Action                  pending_{};   // panel click, applied on the next update
    int                            nav_click_ = -1;
};

} // namespace studioshell
