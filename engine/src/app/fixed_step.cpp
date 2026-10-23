#include <nova/app/fixed_step.hpp>

#include <cmath>

namespace nova {

i32 FixedStepClock::advance(f64 frameSeconds) {
    if (frameSeconds > 0) accumulator_ += frameSeconds;
    // Tiny epsilon so exact multiples of the step don't lose a step to rounding.
    auto steps = i32(std::floor((accumulator_ + 1e-9) / step_));
    if (steps > maxSteps_) {
        accumulator_ = std::fmod(accumulator_, step_);
        return maxSteps_;
    }
    accumulator_ -= steps * step_;
    if (accumulator_ < 0) accumulator_ = 0;
    return steps;
}

} // namespace nova
