#include <nova/app/app_options.hpp>

#include <charconv>
#include <cstring>

namespace nova {

AppOptions parseAppOptions(int argc, char** argv) {
    AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--autoplay") == 0) {
            options.autoplay = true;
        } else if (std::strcmp(arg, "--fail-on-alloc") == 0) {
            options.failOnAlloc = true;
        } else if (std::strcmp(arg, "--screenshot") == 0 && i + 1 < argc) {
            options.screenshotPath = argv[++i];
        } else if (std::strcmp(arg, "--frames") == 0 && i + 1 < argc) {
            const char* value = argv[++i];
            i32 frames = -1;
            auto [end, ec] = std::from_chars(value, value + std::strlen(value), frames);
            if (ec == std::errc{} && *end == '\0' && frames > 0) options.maxFrames = frames;
        }
        // Unknown flags (e.g. ones the OS adds when launching an app) are ignored.
    }
    return options;
}

} // namespace nova
