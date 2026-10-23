#pragma once

namespace nova::detail {
// Logs the failure and, in debug builds, breaks into the debugger.
void assertFailed(const char* expr, const char* msg, const char* file, int line);
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
