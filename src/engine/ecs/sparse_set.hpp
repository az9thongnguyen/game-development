// =============================================================================
//  engine/ecs/sparse_set.hpp  —  generic component storage + type-erasure base
// =============================================================================
//  One SparseSet<T> per component type. It is the generalization of the farm ECS's
//  Pool<T> (ch28): a dense, packed array of components (great for iteration) plus a
//  sparse array mapping an entity INDEX to its dense slot. add/get/has/remove are
//  O(1); remove is swap-and-pop (does not preserve order). The IPool base lets the
//  Registry remove/has a component by entity index WITHOUT knowing T (type erasure).
//
//   sparse_:  index → slot (or kTombstone)        owners_: slot → index
//   data_:    slot  → component (packed)           ← iterate THIS contiguously
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace ecs {

inline constexpr std::uint32_t kTombstone = 0xFFFFFFFFu;   // "this index has no slot"

// Type-erased interface the Registry holds (one per component type). Non-copyable,
// non-movable (it is always owned through a unique_ptr<IPool>) — prevents slicing.
class IPool {
public:
    IPool()                        = default;
    virtual ~IPool()               = default;
    IPool(const IPool&)            = delete;
    IPool& operator=(const IPool&) = delete;
    IPool(IPool&&)                 = delete;
    IPool& operator=(IPool&&)      = delete;

    virtual bool        has(std::uint32_t index) const = 0;
    virtual void        remove(std::uint32_t index)    = 0;
    virtual std::size_t size() const                   = 0;
};

template <typename T>
class SparseSet : public IPool {
public:
    bool has(std::uint32_t index) const override {
        return index < sparse_.size() && sparse_[index] != kTombstone;
    }

    // Insert or overwrite; returns a reference to the stored component.
    T& add(std::uint32_t index, T value) {
        if (index >= sparse_.size()) sparse_.resize(index + 1, kTombstone);
        if (sparse_[index] != kTombstone) {                 // overwrite in place
            data_[sparse_[index]] = std::move(value);
            return data_[sparse_[index]];
        }
        sparse_[index] = static_cast<std::uint32_t>(data_.size());
        owners_.push_back(index);
        data_.push_back(std::move(value));
        return data_.back();
    }

    // NOTE: the returned pointer is into a std::vector and is INVALIDATED by any
    // later add() on this same component type (a push_back may reallocate). Don't
    // hold it across add()s — re-fetch via get().
    T*       get(std::uint32_t index)       { return has(index) ? &data_[sparse_[index]] : nullptr; }
    const T* get(std::uint32_t index) const { return has(index) ? &data_[sparse_[index]] : nullptr; }

    void remove(std::uint32_t index) override {
        if (!has(index)) return;
        const std::uint32_t slot = sparse_[index];
        const std::uint32_t last = static_cast<std::uint32_t>(data_.size()) - 1u;
        if (slot != last) {                                 // swap the hole with the last
            data_[slot]            = std::move(data_[last]);
            owners_[slot]          = owners_[last];
            sparse_[owners_[slot]] = slot;                  // repoint the moved entity
        }
        data_.pop_back();
        owners_.pop_back();
        sparse_[index] = kTombstone;
    }

    std::size_t size() const override { return data_.size(); }

    const std::vector<std::uint32_t>& owners() const { return owners_; }  // slot → entity index
    std::vector<T>&                   data()         { return data_; }

private:
    std::vector<std::uint32_t> sparse_;   // entity index → dense slot (kTombstone = none)
    std::vector<std::uint32_t> owners_;   // dense slot → entity index
    std::vector<T>             data_;      // dense slot → component (packed)
};

} // namespace ecs
