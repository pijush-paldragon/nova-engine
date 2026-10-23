#include <nova/core/frame_arena.hpp>
#include <nova/core/log.hpp>

#include <cstdint>

namespace nova {
namespace {

constexpr size_t kGrowGranularity = 64 * 1024;

size_t alignUp(size_t value, size_t align) { return (value + align - 1) & ~(align - 1); }

} // namespace

FrameArena::FrameArena(size_t capacity)
    : primary_(std::make_unique<std::byte[]>(capacity)), capacity_(capacity) {}

void* FrameArena::allocate(size_t bytes, size_t align) {
    auto base = reinterpret_cast<std::uintptr_t>(primary_.get());
    size_t offset = alignUp(base + used_, align) - base;
    if (offset + bytes <= capacity_) {
        used_ = offset + bytes;
        return primary_.get() + offset;
    }

    if (!warnedThisFrame_) {
        log::warn("FrameArena overflow: %zu bytes requested, %zu/%zu used; growing next frame",
                  bytes, used_, capacity_);
        warnedThisFrame_ = true;
    }
    // Over-allocate by the alignment so the returned pointer can be aligned inside the block.
    auto& block = overflow_.emplace_back(std::make_unique<std::byte[]>(bytes + align));
    overflowBytes_ += bytes + align;
    auto raw = reinterpret_cast<std::uintptr_t>(block.get());
    return reinterpret_cast<void*>(alignUp(raw, align));
}

void FrameArena::reset() {
    if (overflowBytes_ > 0) {
        capacity_ = alignUp(capacity_ + overflowBytes_, kGrowGranularity);
        primary_ = std::make_unique<std::byte[]>(capacity_);
        overflow_.clear();
        overflowBytes_ = 0;
    }
    used_ = 0;
    warnedThisFrame_ = false;
}

} // namespace nova
