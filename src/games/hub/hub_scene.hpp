// =============================================================================
//  games/hub/hub_scene.hpp  —  the graphical Hub shell (--hub-ui)
// =============================================================================
//  A window that renders one project's aggregate status + next recommended action,
//  the same content the headless `--hub` prints (both go through engine::hub_lines,
//  and both decide the next step with engine::next_action).
//
//  This is only the SDL-touching glue: it builds the view via engine::build_hub_view,
//  hands it to the shared hubui::draw_hub_panel, and routes the resulting action back
//  into engine::release ops. The Studio shell's Hub tab does exactly the same with the
//  same panel, so there is one Hub rendering, not two that drift.
// =============================================================================
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "engine/hub/hub.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "games/hub/hub_panel.hpp"

namespace hubui {

class HubScene : public engine::Scene {
public:
    explicit HubScene(std::string project_path);
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    void rebuild();                          // re-read the project + release store
    void run(const Action& a);               // perform whatever the panel/keys asked for

    std::string                    path_;
    std::vector<std::string>       known_entries_;
    std::optional<engine::HubView> view_;
    std::string                    flash_;     // last op result message
    double                         flash_t_ = 0;  // seconds the flash stays visible
    ui::Context                    ui_;
    Action                         pending_{};    // keyboard action, applied on the next update
};

} // namespace hubui
