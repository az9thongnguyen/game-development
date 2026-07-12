// =============================================================================
//  games/hub/hub_scene.hpp  —  the graphical Hub shell (--hub-ui)
// =============================================================================
//  A window that renders one project's aggregate status + next recommended action,
//  the same content the headless `--hub` prints (both go through engine::hub_lines).
//  This is only the SDL-touching glue: it builds the view via engine::build_hub_view
//  and draws each line through Renderer2D. All the logic lives in the tested cores.
//  ponytail: read-only first — it shows the recommended verb; you run it from the CLI
//  and press R to refresh. Interactive mutation is a later slice once the domain ops
//  are extracted from main.cpp behind a callable interface.
// =============================================================================
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "engine/hub/hub.hpp"
#include "engine/scene.hpp"

namespace hubui {

class HubScene : public engine::Scene {
public:
    explicit HubScene(std::string project_path);
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    void rebuild();                          // re-read the project + release store

    std::string                    path_;
    std::vector<std::string>       known_entries_;
    std::optional<engine::HubView> view_;
    std::string                    flash_;     // last op result message
    double                         flash_t_ = 0;  // seconds the flash stays visible
    int                            h_ = 480;   // footer baseline; width comes from the framebuffer
};

} // namespace hubui
