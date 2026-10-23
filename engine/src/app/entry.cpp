// Compiled into each game target (not the static library) by nova_add_game, so the linker
// always keeps the SDL entry points and the allocation-tracking operator new.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include <nova/core/alloc_stats.hpp>

#include <cstdlib>
#include <new>

#if defined(NOVA_REQUIRE_PACK)
#define NOVA_REQUIRE_PACK_VALUE 1
#else
#define NOVA_REQUIRE_PACK_VALUE 0
#endif

namespace nova::detail {
void setPackRequired(bool required);
SDL_AppResult appInit(int argc, char** argv);
SDL_AppResult appIterate();
SDL_AppResult appEvent(const SDL_Event& event);
void appQuit();
} // namespace nova::detail

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    (void)appstate;
    nova::detail::setPackRequired(NOVA_REQUIRE_PACK_VALUE); // pack builds: a missing game.pak is tampering
    return nova::detail::appInit(argc, argv);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    (void)appstate;
    return nova::detail::appIterate();
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    (void)appstate;
    return nova::detail::appEvent(*event);
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)appstate;
    (void)result;
    nova::detail::appQuit();
}

#if defined(NOVA_TRACK_ALLOCS)
// Debug builds count every C++ heap allocation so the App can flag gameplay frames that allocate.
void* operator new(std::size_t size) {
    nova::memory::recordAllocation();
    if (void* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif
