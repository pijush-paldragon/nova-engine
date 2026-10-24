#pragma once

#include <nova/core/types.hpp>

#include <array>
#include <string_view>

struct SDL_Renderer;

namespace nova {

// Rolling average of the last 60 frame times.
class FpsCounter {
public:
    void add(f32 dt);
    f32 fps() const;
    f32 avgMs() const;

private:
    std::array<f32, 60> samples_{};
    size_t next_ = 0;
    size_t count_ = 0;
    f32 sum_ = 0;
};

struct FrameStats {
    f32 fps;
    f32 frameMs;
    const char* backend;
    u32 drawCalls;
    u32 entities;
    u32 bodies;
    u64 allocsLastFrame;
};

// Draws the stats in the top-left corner with SDL's built-in debug font.
void drawDebugOverlay(SDL_Renderer* renderer, const FrameStats& stats, f32 pixelScale);

// A red bar across the top of the window with a message (live-reload errors). No font needed.
void drawErrorBanner(SDL_Renderer* renderer, std::string_view message, f32 pixelScale);

} // namespace nova
