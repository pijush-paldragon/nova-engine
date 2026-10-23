#include <nova/core/random.hpp>
#include <nova/render/camera.hpp>

#include <algorithm>

namespace nova {

ScreenFit computeFit(Vec2 windowPx, Vec2 design) {
    ScreenFit fit;
    fit.windowPx = windowPx;
    fit.scale = std::min(windowPx.x / design.x, windowPx.y / design.y);
    if (fit.scale <= 0) fit.scale = 1;
    f32 w = windowPx.x / fit.scale;
    f32 h = windowPx.y / fit.scale;
    fit.visible = {(design.x - w) * 0.5f, (design.y - h) * 0.5f, w, h};
    return fit;
}

void Camera2D::shake(f32 amplitudePx, f32 seconds) {
    // A new shake never weakens one already in progress.
    if (amplitudePx * seconds >= shakeAmplitude_ * shakeRemaining_) {
        shakeAmplitude_ = amplitudePx;
        shakeDuration_ = seconds;
        shakeRemaining_ = seconds;
    }
}

void Camera2D::update(f32 dt, Random& random) {
    if (shakeRemaining_ <= 0) {
        shakeOffset_ = {};
        return;
    }
    shakeRemaining_ = std::max(0.f, shakeRemaining_ - dt);
    if (shakeRemaining_ == 0) {
        shakeOffset_ = {};
        return;
    }
    f32 strength = shakeAmplitude_ * (shakeRemaining_ / shakeDuration_);
    Vec2 dir{random.range(-1, 1), random.range(-1, 1)};
    shakeOffset_ = dir.normalized() * strength;
}

} // namespace nova
