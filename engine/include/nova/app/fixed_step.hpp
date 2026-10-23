#pragma once

#include <nova/core/types.hpp>

namespace nova {

// Fixed-timestep accumulator. advance() returns how many simulation steps to run this frame;
// alpha() is how far the render is between the last two steps. When a frame would need more
// than maxSteps (a stall, or resuming from the background), the backlog is dropped instead of
// fast-forwarding, so the game never "spirals" trying to catch up.
class FixedStepClock {
public:
    explicit FixedStepClock(f64 step = 1.0 / 60, i32 maxSteps = 5) : step_(step), maxSteps_(maxSteps) {}

    i32 advance(f64 frameSeconds);
    f32 alpha() const { return f32(accumulator_ / step_); }
    void reset() { accumulator_ = 0; }
    f64 step() const { return step_; }

private:
    f64 step_;
    i32 maxSteps_;
    f64 accumulator_ = 0;
};

} // namespace nova
