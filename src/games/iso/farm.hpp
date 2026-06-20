// =============================================================================
//  games/iso/farm.hpp  —  the simulation MODEL (no rendering, no SDL)
// =============================================================================
//  Farm ties the pieces together: a TileMap (the floor), a World (the ECS, holding
//  every object + the farmer), and an OCCUPANCY grid that says which object sits on
//  each tile. The occupancy grid is a DERIVED acceleration structure — the ECS is
//  the source of truth — kept in sync on every edit and rebuilt on load. It gives
//  O(1) "what's on this tile?" for placement and for A* walkability.
//
//  Rule: at most ONE object per tile (keeps placement + depth-sort simple). The
//  farmer is separate — it is an entity but never sits in the occupancy grid.
//
//  This class is pure logic so tests drive it without a window; the renderer and
//  the scene only READ it (plus call its verbs).
// =============================================================================
#pragma once

#include <cstddef>
#include <vector>

#include "engine/iso.hpp"
#include "games/iso/ecs.hpp"
#include "games/iso/objkind.hpp"
#include "games/iso/tilemap.hpp"

namespace iso {

class Farm {
public:
    Farm(int w, int h);

    int width()  const { return map_.width(); }
    int height() const { return map_.height(); }

    const TileMap& map()   const { return map_; }
    const World&   world() const { return world_; }

    // ---- terrain editing ----
    void    set_terrain(int x, int y, Terrain t) { map_.set(x, y, t); }
    Terrain terrain_at(int x, int y) const { return map_.at(x, y); }

    // ---- object editing (at most one object per tile) ----
    Entity object_at(int x, int y) const;                  // kInvalid if none/oob
    Entity place_object(int x, int y, ObjKind kind);       // replaces any existing
    void   remove_object(int x, int y);

    // ---- the farmer ----
    Entity farmer() const { return farmer_; }
    Vec2i  farmer_cell() const;                            // rounded grid cell
    Entity spawn_farmer(int x, int y);                     // create or reposition
    bool   command_farmer(int gx, int gy);                 // A* route; false if none

    // ---- simulation ----
    bool walkable(int x, int y) const;   // terrain not water AND no blocking object
    void update(double dt);              // advance the farmer along its path

    // ---- lifecycle ----
    void reset(int w, int h);            // blank grass field of the given size
    void clear() { reset(width(), height()); }
    void reset_default();                // a hand-built starter scene

private:
    int  idx(int x, int y) const { return y * map_.width() + x; }
    void rebuild_occupancy();

    TileMap             map_;
    World               world_;
    std::vector<Entity> occ_;            // w*h: object entity per tile (kInvalid = none)
    Entity              farmer_ = kInvalid;
};

} // namespace iso
