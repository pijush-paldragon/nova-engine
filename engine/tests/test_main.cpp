#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <nova/platform/file.hpp>

#include <SDL3/SDL.h>

int main(int argc, char** argv) {
    // Tests read the sample game's generated assets straight from the source tree and run
    // headless: dummy video (software renderer) and dummy audio.
    nova::setAssetRoot(NOVA_TEST_ASSET_ROOT);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    doctest::Context context(argc, argv);
    int result = context.run();
    SDL_Quit();
    return result;
}
