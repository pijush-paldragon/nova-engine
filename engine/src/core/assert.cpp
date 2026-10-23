#include <nova/core/assert.hpp>
#include <nova/core/log.hpp>

#include <SDL3/SDL_assert.h>

#include <atomic>

namespace nova::detail {
namespace {
std::atomic<u32> gFailures{0};
std::atomic<bool> gBreak{true};
} // namespace

void assertFailed(const char* expr, const char* msg, const char* file, int line) {
    gFailures.fetch_add(1, std::memory_order_relaxed);
    log::error("Assertion failed: %s (%s) at %s:%d", expr, msg, file, line);
#if defined(NOVA_DEBUG)
    if (gBreak.load(std::memory_order_relaxed)) SDL_TriggerBreakpoint();
#endif
}

u32 assertFailureCount() { return gFailures.load(std::memory_order_relaxed); }

void setAssertBreakEnabled(bool enabled) { gBreak.store(enabled, std::memory_order_relaxed); }

} // namespace nova::detail
