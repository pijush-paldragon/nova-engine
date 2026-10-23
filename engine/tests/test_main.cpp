#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <nova/platform/file.hpp>

#include <SDL3/SDL.h>

#include <nova/core/alloc_stats.hpp>
#include <nova/core/assert.hpp>

#include <cstdlib>
#include <new>

#if defined(NOVA_TRACK_ALLOCS)
// Same counting allocator games get from entry.cpp, so tests can assert "no allocations".
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

int main(int argc, char** argv) {
    // Tests read the sample game's generated assets straight from the source tree and run
    // headless: dummy video (software renderer) and dummy audio.
    nova::detail::setAssertBreakEnabled(false); // tests deliberately exercise failure paths
    nova::setAssetRoot(NOVA_TEST_ASSET_ROOT);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    doctest::Context context(argc, argv);
    int result = context.run();
    SDL_Quit();
    return result;
}
