#pragma once

#include <nova/core/types.hpp>

namespace nova {

// Fast deterministic PRNG (xorshift64*). Not for cryptography.
class Random {
public:
    explicit Random(u64 seed) : state_(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    u32 next() {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return u32((state_ * 0x2545F4914F6CDD1Dull) >> 32);
    }

    // Uniform in [lo, hi).
    f32 range(f32 lo, f32 hi) { return lo + (hi - lo) * (f32(next() >> 8) * (1.0f / 16777216.0f)); }

    // Uniform in [lo, hiInclusive].
    i32 rangeInt(i32 lo, i32 hiInclusive) {
        u32 span = u32(hiInclusive - lo) + 1u;
        return lo + i32(next() % span);
    }

private:
    u64 state_;
};

} // namespace nova
