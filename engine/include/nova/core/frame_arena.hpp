#pragma once

#include <nova/core/types.hpp>

#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <vector>

namespace nova {

// Linear allocator reset once per frame. Allocations are never freed individually.
// When a frame needs more than the primary block, overflow blocks are taken from the heap
// (with a warning) and the primary block grows on the next reset, so the steady state is
// allocation-free and nothing ever fails.
class FrameArena {
public:
    explicit FrameArena(size_t capacity);

    void* allocate(size_t bytes, size_t align = alignof(std::max_align_t));

    template <class T>
    std::span<T> allocArray(size_t count) {
        static_assert(std::is_trivially_destructible_v<T>, "FrameArena never runs destructors");
        if (count == 0) return {};
        T* data = static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
        for (size_t i = 0; i < count; ++i) ::new (data + i) T();
        return {data, count};
    }

    void reset();

    size_t used() const { return used_ + overflowBytes_; }
    size_t capacity() const { return capacity_; }
    size_t overflowBytesLastFrame() const { return overflowBytes_; }

private:
    std::unique_ptr<std::byte[]> primary_;
    size_t capacity_;
    size_t used_ = 0;
    std::vector<std::unique_ptr<std::byte[]>> overflow_;
    size_t overflowBytes_ = 0;
    bool warnedThisFrame_ = false;
};

} // namespace nova
