// =============================================================================
//  games/colony/colony_scene.hpp  —  the --colony scene (A + D + F + BaaS)
// =============================================================================
//  Wires the engine-core systems into a playable scene:
//    • A (mem::FrameAllocator): the per-frame depth-sort drawable list is built in
//      frame scratch (flip + alloc each frame)
//    • D (assets::AssetCache): the agent sprite is loaded through the cache and
//      hot-reloaded (reload_changed) each frame
//    • F (ui::Context): an immediate-mode panel drives the sim
//  The Sim (B ECS + C jobs) does the simulation. Reuses engine/iso projection.
//
//  It also talks to the Game BaaS through the C++ SDK (gbaas): guest login, submit
//  the colony's score, and show the global leaderboard — the same non-blocking
//  client works native (libcurl) and web (emscripten_fetch), pumped each frame.
// =============================================================================
#pragma once

#include <memory>
#include <string>

#include "engine/asset_cache.hpp"
#include "engine/image.hpp"
#include "engine/memory/frame.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "gbaas/gbaas.h"
#include "games/colony/colony.hpp"

namespace colony {

class ColonyScene : public engine::Scene {
public:
    ColonyScene();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    iso::Vec2i hovered_cell(const platform::InputState& in) const;

    // BaaS actions (results delivered via client_.update() pumped each frame).
    static std::string default_base_url();
    void               login();
    void               submit_score();
    void               refresh_board();
    void               cloud_save();   // serialize the sim → saves().put("colony", …)
    void               cloud_load();   // saves().get("colony") → rebuild the sim
    void               refresh_wood(); // inventory().get("wood") → wood_

    Sim                          sim_;
    ui::Context                  ui_;
    mem::FrameAllocator          frame_;
    assets::AssetCache           cache_;
    std::shared_ptr<gfx::Image>  sprite_;

    // ---- online (BaaS) state ----
    gbaas::Client client_;
    bool          online_     = false;
    bool          board_open_ = false;
    std::string   status_     = "connecting...";
    long long     my_score_   = 0;
    int           my_rank_    = 0;
    long long     wood_       = 0;
    std::string   motd_;         // remote config value
    std::string   event_name_;  // first active live event
    gbaas::Board  board_;

    float      ox_ = 480.0f, oy_ = 60.0f;   // iso camera offset
    bool       running_ = true;
    float      speed_   = 3.0f;
    iso::Vec2i hovered_ = {-1, -1};
    int        w_ = 960, h_ = 600;
    double     fps_ = 60.0;
};

} // namespace colony
