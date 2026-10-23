#include <nova/core/alloc_stats.hpp>

#include <atomic>

namespace nova::memory {
namespace {

std::atomic<u64> gAllocations{0};

} // namespace

u64 allocationCount() { return gAllocations.load(std::memory_order_relaxed); }

void recordAllocation() { gAllocations.fetch_add(1, std::memory_order_relaxed); }

} // namespace nova::memory
