// =============================================================================
//  games/sandbox/world.hpp  —  the sandbox simulation MODEL (pure, no SDL)
// =============================================================================
//  A tiny declarative playground on top of the generic ecs::Registry. An "actor"
//  is an entity carrying data-only behavior components; the "program" is which
//  components you attach, not a script. World::tick advances every behavior one
//  fixed step in a fixed order, buffering structural edits so no system mutates a
//  pool it is iterating. Fully unit-tested in tests/test_sandbox.cpp (no window).
// =============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "engine/color.hpp"
#include "engine/ecs/registry.hpp"

namespace sandbox {

// ---- components (all plain data on ecs::Registry) --------------------------
struct Transform2D { float x = 0, y = 0, rot = 0, scale = 1; };  // center px, radians
struct Body        { float w = 24, h = 24; };                    // AABB full size, px
struct Sprite      { gfx::Color color = gfx::rgb(220, 200, 120); bool round = false;
                     std::string texture;                         // "" = flat colour
                     int frames = 1; float fps = 8.0f; };         // >1 = animated vertical sheet
struct Mover       { float vx = 0, vy = 0; };                    // px/s
struct Spinner     { float omega = 0; };                         // rad/s
struct Bouncer     {};                                           // tag: reflect at bounds
struct Lifetime    { float ttl = 2.0f; };                        // seconds remaining
struct Tag         { int id = 0; };                              // 0 = untagged

enum class Action { DestroySelf, DestroyOther, SpawnProto };

// Flat template shared by the palette, Spawner, OnOverlap, and (de)serialization.
// Flat = it never carries a Spawner/OnOverlap of its own, which bounds recursion.
struct Archetype {
    std::string name  = "actor";
    gfx::Color  color = gfx::rgb(220, 200, 120);
    float w = 24, h = 24;
    bool  round = false;
    bool  mover = false;    float vx = 0, vy = 0;
    bool  spinner = false;  float omega = 0;
    bool  bouncer = false;
    bool  lifetime = false; float ttl = 2.0f;
    int   tag = 0;
    std::string texture;    // "" = flat colour; else a Texture Lab asset name (studio_NN)
    int   frames = 1;       // >1 = the texture is a vertical N-frame sheet, animated
    float fps = 8.0f;       // playback rate when frames>1
};

// Behaviors that own a proto live outside Archetype (added after spawn).
struct Spawner   { float interval = 1.0f, timer = 0; Archetype proto; };
struct OnOverlap { int other_tag = 0; Action action = Action::DestroySelf; Archetype proto; };

class World {
public:
    float bounds_w = 936, bounds_h = 560;

    // The single spawn funnel: always Transform2D+Body+Sprite, then flagged behaviors.
    ecs::Entity spawn(const Archetype& a, float x, float y);

    // Advance every behavior one step; structural edits are buffered and applied last.
    void tick(float dt);

    std::size_t   alive() const { return reg.alive(); }
    ecs::Registry reg;
};

} // namespace sandbox
