#pragma once

#include <nova/core/types.hpp>

namespace nova::detail {
// Logs and counts the failure and, in debug builds, breaks into the debugger.
void assertFailed(const char* expr, const char* msg, const char* file, int line);
// Number of failed asserts/verifies so far; frame-limited runs (smoke tests) fail if non-zero.
u32 assertFailureCount();
// The unit-test runner turns breaking off so it can exercise failure paths.
void setAssertBreakEnabled(bool enabled);
} // namespace nova::detail

// Debug-only check; compiled out in release.
#if defined(NOVA_DEBUG)
#define NOVA_ASSERT(cond, msg)                                                  \
    do {                                                                        \
        if (!(cond)) ::nova::detail::assertFailed(#cond, msg, __FILE__, __LINE__); \
    } while (0)
#else
#define NOVA_ASSERT(cond, msg) ((void)0)
#endif

// Always-evaluated check that yields the condition, so callers can recover:
//   if (!NOVA_VERIFY(ptr, "missing")) return;
#define NOVA_VERIFY(cond, msg)                                                       \
    ((cond) ? true : (::nova::detail::assertFailed(#cond, msg, __FILE__, __LINE__), false))
