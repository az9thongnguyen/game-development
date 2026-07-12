// =============================================================================
//  games/studio_shell/studio_shell_scene.hpp  —  the Studio shell (--shell)
// =============================================================================
//  Horizon 1's "shared Studio shell and dock/navigation model": a window with a left
//  nav rail (Hub / Learn / About) and a main panel that renders the selected section.
//  The Hub section IS the interactive release controller — it reuses the same tested
//  engine::hub_lines content and engine::release ops as --hub / --hub-ui, so there is no
//  second copy of "what the hub says" or "how to publish". This scene is only the frame
//  + navigation; every panel's substance lives in a tested core.
//  ponytail: three sections, the shell that hosts them, and nothing more — a dock manager,
//  thumbnails, and folding the existing Labs in come when a second author actually needs them.
// =============================================================================
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "engine/hub/hub.hpp"
#include "engine/scene.hpp"

namespace studioshell {

class StudioShellScene : public engine::Scene {
public:
    explicit StudioShellScene(std::string project_path);
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    enum Section { Hub = 0, Learn, About, SectionCount };

    void rebuild_hub();

    std::string                    project_path_;
    std::vector<std::string>       known_entries_;
    int                            section_ = Hub;
    std::optional<engine::HubView> hub_;
    std::string                    flash_;
    double                         flash_t_ = 0;
    int                            h_ = 560;   // panel height; width taken from the framebuffer
};

} // namespace studioshell
