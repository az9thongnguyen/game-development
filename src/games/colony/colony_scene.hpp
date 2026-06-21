// =============================================================================
//  games/colony/colony_scene.hpp  —  the --colony scene (A + D + F integration)
// =============================================================================
//  Wires the engine-core systems into a playable scene:
//    • A (mem::FrameAllocator): the per-frame depth-sort drawable list is built in
//      frame scratch (flip + alloc each frame)
//    • D (assets::AssetCache): the agent sprite is loaded through the cache and
//      hot-reloaded (reload_changed) each frame
//    • F (ui::Context): an immediate-mode panel drives the sim
//  The Sim (B ECS + C jobs) does the simulation. Reuses engine/iso projection.
// =============================================================================
#pragma once

#include <memory>

#include "engine/asset_cache.hpp"
#include "engine/image.hpp"
#include "engine/memory/frame.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "games/colony/colony.hpp"

namespace colony {

class ColonyScene : public engine::Scene {
public:
    ColonyScene();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    iso::Vec2i hovered_cell(const platform::InputState& in) const;

    Sim                          sim_;
    ui::Context                  ui_;
    mem::FrameAllocator          frame_;
    assets::AssetCache           cache_;
    std::shared_ptr<gfx::Image>  sprite_;

    float      ox_ = 480.0f, oy_ = 60.0f;   // iso camera offset
    bool       running_ = true;
    float      speed_   = 3.0f;
    iso::Vec2i hovered_ = {-1, -1};
    int        w_ = 960, h_ = 600;
    double     fps_ = 60.0;
};

} // namespace colony
