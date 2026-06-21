// =============================================================================
//  engine/ecs/registry.hpp  —  the ECS World: entities + typed components + views
// =============================================================================
//  The registry owns the entity allocator (index + generation, ch38) and a sparse
//  vector of type-erased component pools (one SparseSet<T> per component type,
//  located by a per-type id). Templated component ops live here; the non-template
//  entity machinery lives in registry.cpp.
//
//  Usage:
//      ecs::Registry r;
//      ecs::Entity e = r.create();
//      r.add<Position>(e, {1,2});
//      r.add<Velocity>(e, {0,1});
//      r.view<Position, Velocity>([](ecs::Entity, Position& p, Velocity& v){ p.x += v.x; });
//      r.destroy(e);
// =============================================================================
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "engine/ecs/entity.hpp"
#include "engine/ecs/sparse_set.hpp"

namespace ecs {

namespace detail {
// A process-wide, monotonically-increasing id per component type. Single binary →
// stable; the id indexes the registry's pool vector.
inline std::size_t next_type_id() {
    static std::size_t counter = 0;
    return counter++;
}
template <typename T>
std::size_t type_id() {
    static const std::size_t id = next_type_id();
    return id;
}
} // namespace detail

class Registry {
public:
    Registry()                           = default;
    Registry(const Registry&)            = delete;   // owns unique_ptr pools
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&)                 = default;   // movable is fine + intentional
    Registry& operator=(Registry&&)      = default;

    // ---- entities (registry.cpp) ----
    Entity      create();
    void        destroy(Entity e);
    bool        valid(Entity e) const;
    std::size_t alive() const { return alive_; }

    // ---- components ----
    template <typename T>
    T& add(Entity e, T value = {}) {
        assert(valid(e) && "Registry::add: entity is not valid");
        return pool<T>().add(e.index, std::move(value));
    }

    template <typename T>
    T* get(Entity e) {
        if (!valid(e)) return nullptr;
        SparseSet<T>* p = pool_ptr<T>();
        return p ? p->get(e.index) : nullptr;
    }

    template <typename T>
    bool has(Entity e) const {
        if (!valid(e)) return false;
        const SparseSet<T>* p = pool_ptr<T>();
        return p && p->has(e.index);
    }

    template <typename T>
    void remove(Entity e) {
        if (!valid(e)) return;
        if (SparseSet<T>* p = pool_ptr<T>()) p->remove(e.index);
    }

    // Iterate every entity that has ALL of First, Rest...; the callback receives
    // (Entity, First&, Rest&...). The FIRST component drives the loop, so list the
    // rarest component first for best performance (auto smallest-pool is an exercise).
    //
    // PRECONDITION: the callback must not remove/destroy the entity it is handed, nor
    // add a component of any iterated type — both can move the storage and dangle the
    // references the callback holds. Defer such structural edits to after the view.
    template <typename First, typename... Rest, typename Fn>
    void view(Fn&& fn) {
        SparseSet<First>* driver = pool_ptr<First>();
        if (!driver) return;
        if (((pool_ptr<Rest>() == nullptr) || ...)) return;   // a required pool is absent

        // Copy the owners list up front so adding entities mid-iteration is safe.
        const std::vector<std::uint32_t> owners = driver->owners();
        for (const std::uint32_t i : owners) {
            if (!driver->has(i)) continue;                    // skip if since removed
            if (((pool_ptr<Rest>()->has(i)) && ...)) {
                std::forward<Fn>(fn)(Entity{i, generations_[i]},
                                     *driver->get(i), *pool_ptr<Rest>()->get(i)...);
            }
        }
    }

private:
    template <typename T>
    SparseSet<T>& pool() {
        const std::size_t id = detail::type_id<T>();
        if (id >= pools_.size()) pools_.resize(id + 1);
        if (!pools_[id]) pools_[id] = std::make_unique<SparseSet<T>>();
        return *static_cast<SparseSet<T>*>(pools_[id].get());
    }

    // Non-creating pool lookup (nullptr if T has never been added). Note: type_id<T>()
    // consumes a global per-type id on first use even from the const path — harmless,
    // ids are per-type across all registries.
    template <typename T>
    SparseSet<T>* pool_ptr() {
        const std::size_t id = detail::type_id<T>();
        return (id < pools_.size() && pools_[id])
                   ? static_cast<SparseSet<T>*>(pools_[id].get())
                   : nullptr;
    }
    template <typename T>
    const SparseSet<T>* pool_ptr() const {
        const std::size_t id = detail::type_id<T>();
        return (id < pools_.size() && pools_[id])
                   ? static_cast<const SparseSet<T>*>(pools_[id].get())
                   : nullptr;
    }

    std::vector<std::uint32_t>           generations_;   // per index: current generation
    std::vector<std::uint32_t>           free_;          // recycled indices
    std::vector<std::unique_ptr<IPool>>  pools_;         // per type_id: component pool
    std::size_t                          alive_ = 0;
};

} // namespace ecs
