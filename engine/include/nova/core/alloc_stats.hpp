#pragma once

#include <nova/core/types.hpp>

// Heap allocation counter. In builds with NOVA_TRACK_ALLOCS, the global operator new
// (defined in the game's entry point) calls recordAllocation(), so the app can detect
// gameplay frames that allocate.
namespace nova::memory {

u64 allocationCount();
void recordAllocation();

} // namespace nova::memory
