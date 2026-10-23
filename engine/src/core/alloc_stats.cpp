#include <nova/core/alloc_stats.hpp>

#include <atomic>

namespace nova::memory {
namespace {

std::atomic<u64> gAllocations{0};
std::atomic<bool> gTrap{false};
thread_local bool tIsGameThread = false;
thread_local int tDriverDepth = 0;

} // namespace

void markGameThread() { tIsGameThread = true; }

u64 allocationCount() { return gAllocations.load(std::memory_order_relaxed); }

void recordAllocation() {
    if (!tIsGameThread || tDriverDepth > 0) return;
    gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (gTrap.load(std::memory_order_relaxed)) {
        gTrap.store(false, std::memory_order_relaxed); // trap once
        __builtin_debugtrap();
    }
}

DriverAllocScope::DriverAllocScope() { ++tDriverDepth; }
DriverAllocScope::~DriverAllocScope() { --tDriverDepth; }

void setAllocationTrap(bool enabled) { gTrap.store(enabled, std::memory_order_relaxed); }

} // namespace nova::memory
