// =============================================================================
//  games/iso/ecs.hpp  —  a tiny sparse-set Entity-Component-System
// =============================================================================
//  An ECS stores DATA (components) separately from the THINGS that own them
//  (entities), and runs LOGIC (systems) as plain functions over the component
//  arrays. The win: data of one kind is contiguous, entities are just integer
//  ids, and adding a new behavior means adding a pool + a system, not editing a
//  fat "GameObject" class. This is the smallest version that still teaches the
//  idea — a sparse set per component — not a production archetype ECS.
//
//  Sparse set (per component pool):
//    data_[i]  — the i-th component, packed densely (good for iteration)
//    dense_[i] — which entity owns data_[i]
//    at_[e]    — entity e -> its slot i        (the "sparse" lookup)
//  Removal is swap-and-pop: move the last slot into the hole, fix its index.
//  O(1), but it does NOT preserve insertion order (callers must not assume it).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/color.hpp"
#include "engine/iso.hpp"          // iso::Vec2i
#include "games/iso/objkind.hpp"

namespace iso {

using Entity = std::uint32_t;
inline constexpr Entity kInvalid = 0;   // 0 is never a valid entity

// ---- Components -------------------------------------------------------------
struct Position {                       // where a thing is, in GRID coords
    float x = 0.0f;
    float y = 0.0f;                      // fractional for a moving agent
};

struct Renderable {                     // what to draw + a color tint
    ObjKind   kind = ObjKind::Tree;
    gfx::Color tint = 0xFFFFFFFFu;
};

struct Mover {                          // a walking agent following an A* path
    std::vector<Vec2i> path;            // cells to visit (path[idx] is the next)
    std::size_t        idx   = 0;
    float              speed = 3.0f;    // tiles per second
    bool moving() const { return idx < path.size(); }
};

// ---- A sparse-set component pool -------------------------------------------
template <typename T>
class Pool {
public:
    bool has(Entity e) const { return at_.find(e) != at_.end(); }

    // Insert or overwrite the component for `e`; returns a reference to it.
    T& add(Entity e, const T& v) {
        auto it = at_.find(e);
        if (it != at_.end()) {
            data_[it->second] = v;
            return data_[it->second];
        }
        at_[e] = data_.size();
        dense_.push_back(e);
        data_.push_back(v);
        return data_.back();
    }

    T*       get(Entity e)       { auto it = at_.find(e); return it == at_.end() ? nullptr : &data_[it->second]; }
    const T* get(Entity e) const { auto it = at_.find(e); return it == at_.end() ? nullptr : &data_[it->second]; }

    void remove(Entity e) {
        auto it = at_.find(e);
        if (it == at_.end()) return;
        const std::size_t i    = it->second;
        const std::size_t last = data_.size() - 1;
        if (i != last) {                       // swap the hole with the last slot
            data_[i]       = std::move(data_[last]);
            dense_[i]      = dense_[last];
            at_[dense_[i]] = i;
        }
        data_.pop_back();
        dense_.pop_back();
        at_.erase(it);                         // erase AFTER using `i` (it now dangles)
    }

    std::size_t                size()     const { return data_.size(); }
    const std::vector<Entity>& entities() const { return dense_; }
    std::vector<T>&            data()           { return data_; }
    const std::vector<T>&      data()     const { return data_; }

private:
    std::vector<Entity>                     dense_;   // dense_[i] owns data_[i]
    std::vector<T>                          data_;
    std::unordered_map<Entity, std::size_t> at_;      // entity -> slot
};

// ---- The world: an entity allocator + the component pools ------------------
class World {
public:
    Entity create() {
        const Entity e = next_++;
        alive_.push_back(e);
        return e;
    }

    void destroy(Entity e) {
        positions.remove(e);
        renderables.remove(e);
        movers.remove(e);
        for (std::size_t i = 0; i < alive_.size(); ++i) {
            if (alive_[i] == e) {
                alive_[i] = alive_.back();
                alive_.pop_back();
                break;
            }
        }
    }

    // Wipe everything (used by load): pools reset, ids restart at 1.
    void clear() {
        positions   = {};
        renderables = {};
        movers      = {};
        alive_.clear();
        next_ = 1;
    }

    const std::vector<Entity>& alive() const { return alive_; }

    Pool<Position>   positions;
    Pool<Renderable> renderables;
    Pool<Mover>      movers;

private:
    Entity              next_ = 1;
    std::vector<Entity> alive_;
};

} // namespace iso
