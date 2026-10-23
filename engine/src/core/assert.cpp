#include <nova/core/assert.hpp>
#include <nova/core/log.hpp>

#include <SDL3/SDL_assert.h>

namespace nova::detail {

void assertFailed(const char* expr, const char* msg, const char* file, int line) {
    log::error("Assertion failed: %s (%s) at %s:%d", expr, msg, file, line);
#if defined(NOVA_DEBUG) && !defined(NOVA_TESTING)
    SDL_TriggerBreakpoint();
#endif
}

} // namespace nova::detail
