// =============================================================================
//  games/colony/colony.hpp  —  an iso agent sim built ON the engine core
// =============================================================================
//  The Sim model exists to USE the engine-core systems for real (the audit found
//  ECS/jobs had no game users):
//    • B (ecs::Registry): agents/props are entities with components (no bespoke list)
//    • C (jobs::JobSystem): movement is advanced with parallel_for
//  It reuses engine/iso.hpp (projection) + iso::TileMap/astar (A* is generic). Pure
//  of SDL/renderer, so it is unit-tested headless. (A + D + F are used by the scene.)
// =============================================================================
#pragma once

#include <vector>

#include "engine/color.hpp"
#include "engine/ecs/registry.hpp"
#include "engine/iso.hpp"
#include "engine/jobs/job_system.hpp"
#include "games/iso/tilemap.hpp"

namespace colony {

// ---- components (plain data, stored in the ECS) ----
struct GridPos { float x = 0.0f, y = 0.0f; };           // grid coords (fractional)
struct Visual  { gfx::Color color = 0xFFFFFFFFu; bool is_agent = false; };
struct Agent {
    std::vector<iso::Vec2i> path;
    std::size_t             idx   = 0;
    float                   speed = 3.0f;                // tiles / second
    bool moving() const { return idx < path.size(); }
};

class Sim {
public:
    // workers: -1 = hardware_concurrency()-1 (default); 0 = synchronous (for tests).
    Sim(int w, int h, int workers = -1);

    int                  width()  const { return map_.width(); }
    int                  height() const { return map_.height(); }
    const iso::TileMap&  map()      const { return map_; }
    ecs::Registry&       registry()       { return reg_; }
    const ecs::Registry& registry() const { return reg_; }

    ecs::Entity spawn_agent(int x, int y, gfx::Color color);
    ecs::Entity spawn_prop(int x, int y, gfx::Color color);

    bool walkable(int x, int y) const;   // terrain not water
    void set_goal(int gx, int gy);       // A* a path for EVERY agent
    void update(double dt);              // parallel_for advances all agents

    int  agent_count() const { return agent_count_; }
    void reset_default();                // a starter map + a few agents/props

private:
    iso::TileMap    map_;
    ecs::Registry   reg_;
    jobs::JobSystem jobs_;
    int             agent_count_ = 0;
};

} // namespace colony
