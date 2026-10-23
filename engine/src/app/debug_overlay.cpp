#include "app/debug_overlay.hpp"

#include <nova/core/alloc_stats.hpp>

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nova {

void FpsCounter::add(f32 dt) {
    if (count_ == samples_.size()) sum_ -= samples_[next_];
    else ++count_;
    samples_[next_] = dt;
    sum_ += dt;
    next_ = (next_ + 1) % samples_.size();
}

f32 FpsCounter::fps() const { return (count_ == 0 || sum_ <= 0) ? 0.f : f32(count_) / sum_; }

f32 FpsCounter::avgMs() const { return count_ == 0 ? 0.f : 1000.f * sum_ / f32(count_); }

void drawDebugOverlay(SDL_Renderer* renderer, const FrameStats& s, f32 pixelScale) {
    // SDL's debug glyphs are 8x8 px; scale them up on dense screens.
    f32 k = std::max(1.f, std::round(pixelScale * 2.f));
    char lines[5][64];
    std::snprintf(lines[0], sizeof lines[0], "FPS %.0f  (%.2f ms)", double(s.fps), double(s.frameMs));
    std::snprintf(lines[1], sizeof lines[1], "backend  %s", s.backend);
    std::snprintf(lines[2], sizeof lines[2], "draws    %u", s.drawCalls);
    std::snprintf(lines[3], sizeof lines[3], "entities %u  bodies %u", s.entities, s.bodies);
    std::snprintf(lines[4], sizeof lines[4], "allocs/frame %llu", static_cast<unsigned long long>(s.allocsLastFrame));

    memory::DriverAllocScope driver;
    SDL_SetRenderScale(renderer, k, k);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_FRect bg{4, 4, 200, 5 * 10 + 8};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 120, 255, 160, 255);
    for (int i = 0; i < 5; ++i) SDL_RenderDebugText(renderer, 8, 8 + f32(i) * 10, lines[i]);
    SDL_SetRenderScale(renderer, 1, 1);
}

} // namespace nova
