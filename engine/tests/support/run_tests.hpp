#pragma once

#include <doctest/doctest.h>
#include <nova/core/assert.hpp>
#include <nova/platform/file.hpp>

#include <SDL3/SDL.h>

namespace nova::testing {

// Shared main for engine and game test runners: tests read assets straight from the source
// tree and run headless (dummy video = software renderer, dummy audio).
inline int runTests(int argc, char** argv, const char* assetRoot) {
    detail::setAssertBreakEnabled(false); // tests deliberately exercise failure paths
    setAssetRoot(assetRoot);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    doctest::Context context(argc, argv);
    int result = context.run();
    SDL_Quit();
    return result;
}

} // namespace nova::testing
