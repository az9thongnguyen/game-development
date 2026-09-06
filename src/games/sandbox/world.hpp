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

#include "engine/anim/flipbook.hpp"
#include "engine/color.hpp"
#include "engine/ecs/registry.hpp"
#include "engine/fx/light.hpp"
#include "engine/fx/particles.hpp"

namespace sandbox {

// ---- components (all plain data on ecs::Registry) --------------------------
struct Transform2D { float x = 0, y = 0, rot = 0, scale = 1; };  // center px, radians
struct Body        { float w = 24, h = 24; };                    // AABB full size, px
struct Sprite      { gfx::Color color = gfx::rgb(220, 200, 120); bool round = false;
                     std::string texture;                         // "" = flat colour
                     int frames = 1; float fps = 8.0f;            // >1 = animated vertical sheet
                     bool loop = true;                            // one-shot holds the last frame
                     float t = 0; };                              // playback clock, NOT serialized
struct Mover       { float vx = 0, vy = 0; };                    // px/s
struct Spinner     { float omega = 0; };                         // rad/s
struct Bouncer     {};                                           // tag: reflect at bounds
struct Lifetime    { float ttl = 2.0f; };                        // seconds remaining
struct Tag         { int id = 0; };                              // 0 = untagged

// ---- effect components (chapter 133: the four effect labs, as scene data) ---
// Each was a standalone lab scene demonstrating one engine core with sliders. As
// components they are the same cores, attached to an actor, driven by the same tick
// — which is the difference between a demo of a subsystem and a subsystem you can
// author with. The MODEL stays pure: an Emitter simulates, a Light is arithmetic,
// and a Sound is *recorded* into World::sounds for a host with a device to play.
struct Emitter {
    fx::EmitterConfig cfg;      // rate/speed/spread/gravity/life — the fx lab's sliders
    // One system per emitter, not one shared pool: the pool would have to carry a
    // config, and two emitters with different gravity would then fight over it. The
    // cost is a particle vector per emitter, which is nothing at editor counts.
    fx::ParticleSystem sys{1};
};
// Position comes from Transform2D, so a light rides whatever it is attached to —
// which is the one thing the light lab could not do (its lights were free-floating).
struct Light { float radius = 160; gfx::Color color = gfx::rgb(255, 190, 120);
               float intensity = 1.0f; };
// Heard when the actor is destroyed. "On destroy" is the only event the sandbox
// already has (lifetime expiry, or an OnOverlap that kills), so it needs no new
// trigger concept — and it is the sound a coin makes when something eats it.
struct Sound { float freq = 392.0f, ms = 220.0f, gain = 0.6f; };

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
    bool  loop = true;      // false = play once and hold the last frame
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

    // Sprite flipbook clocks ONLY — no simulation. Split out because an editor shows
    // animated art while it is stopped, and one clock advanced in one place is what
    // keeps the stopped preview and the playing frame from being different numbers.
    void animate(float dt);

    std::size_t   alive() const { return reg.alive(); }
    ecs::Registry reg;

    // What the last tick asked to be heard. The model has no device and must not have
    // one, so it records instead of playing; the host drains this. Cleared per tick.
    std::vector<Sound> sounds;
};

// Which frame of a sheet a sprite is showing right now — the same anim::Flipbook the
// sprite-animation lab used, reading the sprite's own clock. One implementation, so
// the canvas and a test cannot disagree about what is on screen.
inline int sprite_frame(const Sprite& s) {
    return anim::Flipbook{s.frames, s.fps, s.loop, s.t}.frame();
}

} // namespace sandbox
