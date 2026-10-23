#pragma once

#include <nova/core/math.hpp>

namespace nova {

class Random;

// How the fixed design resolution maps onto the actual window.
struct ScreenFit {
    f32 scale = 1;     // window pixels per design unit
    Rect visible;      // visible area in design units (extends past the design area on the long axis)
    Vec2 windowPx;     // window size in pixels
};

// Uniform scale so the whole design area fits; the long axis shows extra world instead of
// letterbox bars. The design area stays centred.
ScreenFit computeFit(Vec2 windowPx, Vec2 design);

class Camera2D {
public:
    Vec2 position{}; // world point shown at the design-area origin
    f32 zoom = 1;

    void shake(f32 amplitudePx, f32 seconds);
    void update(f32 dt, Random& random);
    Vec2 shakeOffset() const { return shakeOffset_; }

    Vec2 screenToWorld(Vec2 design) const { return position + design / zoom; }
    Vec2 worldToScreen(Vec2 world) const { return (world - position) * zoom; }

private:
    f32 shakeAmplitude_ = 0;
    f32 shakeDuration_ = 0;
    f32 shakeRemaining_ = 0;
    Vec2 shakeOffset_{};
};

} // namespace nova
