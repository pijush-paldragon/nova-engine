#pragma once

#include <nova/core/types.hpp>

#include <string>

namespace nova {

// Command-line switches (desktop): --autoplay, --frames N, --fail-on-alloc, --screenshot PATH.
struct AppOptions {
    bool autoplay = false;    // games may drive themselves (smoke tests)
    i32 maxFrames = -1;       // quit after N frames; -1 = run until closed
    bool failOnAlloc = false; // fail if a gameplay frame allocates (after warm-up)
    std::string screenshotPath; // with --frames: save the last frame as PNG
};

AppOptions parseAppOptions(int argc, char** argv);

} // namespace nova
