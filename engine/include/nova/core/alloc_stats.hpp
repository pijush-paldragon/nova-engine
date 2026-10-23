#pragma once

#include <nova/core/types.hpp>

// Heap allocation counter. In builds with NOVA_TRACK_ALLOCS, the global operator new
// (defined in the game's entry point) calls recordAllocation(), so the app can detect
// gameplay frames that allocate. Only allocations on the game thread count: drivers and OS
// services allocate on their own threads, and those are outside the game's control.
namespace nova::memory {

// Marks the calling thread as the game thread (the App does this at startup).
void markGameThread();
u64 allocationCount();
void recordAllocation();
// When set, recordAllocation() breaks into the debugger (find who allocates during gameplay).
void setAllocationTrap(bool enabled);

// Allocations inside this scope are not counted: wrap calls into GPU drivers / OS services
// (e.g. SDL_RenderPresent), whose internal allocations the game cannot avoid. Nestable.
class DriverAllocScope {
public:
    DriverAllocScope();
    ~DriverAllocScope();
    DriverAllocScope(const DriverAllocScope&) = delete;
    DriverAllocScope& operator=(const DriverAllocScope&) = delete;
};

// For game code: wrap mid-scene loading (e.g. building the next level) whose allocations are
// expected, so the --fail-on-alloc check only flags real per-frame gameplay allocations.
using LoadingScope = DriverAllocScope;

} // namespace nova::memory
