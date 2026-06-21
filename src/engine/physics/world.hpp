// =============================================================================
//  engine/physics/world.hpp  —  the physics world + the step pipeline
// =============================================================================
//  Holds the bodies and advances the simulation one fixed step at a time:
//      integrate (gravity) → broadphase (pairs) → narrowphase (contacts) →
//      resolve (impulses + positional correction).
//  +y points DOWN (screen convention), so default gravity is (0, +9.8).
// =============================================================================
#pragma once

#include <cassert>
#include <vector>

#include "engine/math.hpp"
#include "engine/physics/body.hpp"

namespace phys {

class World {
public:
    int  add(const Body& b) { bodies_.push_back(b); return static_cast<int>(bodies_.size()) - 1; }
    Body&       body(int i)       { assert(in_range(i)); return bodies_[static_cast<std::size_t>(i)]; }
    const Body& body(int i) const { assert(in_range(i)); return bodies_[static_cast<std::size_t>(i)]; }
    int  count() const { return static_cast<int>(bodies_.size()); }

    void       set_gravity(math::vec2 g) { gravity_ = g; }
    math::vec2 gravity() const           { return gravity_; }

    // Advance by dt. `iterations` = sequential-impulse passes (more = stabler stacks).
    void step(double dt, int iterations = 4);

private:
    bool in_range(int i) const { return i >= 0 && static_cast<std::size_t>(i) < bodies_.size(); }

    std::vector<Body> bodies_;
    math::vec2        gravity_{0.0f, 9.8f};
};

} // namespace phys
