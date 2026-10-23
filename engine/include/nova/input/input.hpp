#pragma once

#include <nova/core/math.hpp>

#include <array>
#include <bitset>

namespace nova {

inline constexpr u64 kMousePointerId = UINT64_MAX;

// A touch (or the mouse) in design coordinates. A pointer lives from the frame it is
// pressed until the frame after it is released.
struct Pointer {
    u64 id = 0;
    Vec2 pos{};
    Vec2 delta{};      // movement this frame
    Vec2 startPos{};
    f32 heldTime = 0;  // seconds since pressed
    bool pressed = false;  // went down this frame
    bool held = false;     // currently down
    bool released = false; // went up this frame
};

enum class Key : u8 { Left, Right, Up, Down, Space, Enter, Escape, P, R, F1, Count };

// Per-frame input snapshot for game code.
class Input {
public:
    static constexpr int kMaxPointers = 10;

    int pointerCount() const { return count_; }
    const Pointer* pointer(int i) const { return (i >= 0 && i < count_) ? &pointers_[size_t(i)] : nullptr; }
    // The oldest pointer still down; if none are down, the one released this frame.
    const Pointer* primary() const;
    // A quick press-and-release without dragging, this frame.
    bool tapped() const { return tapped_; }

    bool keyDown(Key k) const { return keysDown_[size_t(k)]; }
    bool keyPressed(Key k) const { return keysPressed_[size_t(k)]; }
    // Android back button, or Escape on desktop.
    bool backPressed() const { return backPressed_; }
    bool threeFingerTap() const { return threeFingerTap_; }

protected:
    Input() = default;

    std::array<Pointer, kMaxPointers> pointers_{};
    int count_ = 0;
    bool tapped_ = false;
    bool backPressed_ = false;
    bool threeFingerTap_ = false;
    std::bitset<size_t(Key::Count)> keysDown_;
    std::bitset<size_t(Key::Count)> keysPressed_;
};

} // namespace nova
