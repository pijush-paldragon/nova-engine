#include <nova/ecs/components.hpp>
#include <nova/ecs/tween.hpp>
#include <nova/ecs/world.hpp>

#include <cmath>

namespace nova {

f32 ease(Ease e, f32 t) {
    t = clamp(t, 0.f, 1.f);
    switch (e) {
        case Ease::Linear: return t;
        case Ease::InQuad: return t * t;
        case Ease::OutQuad: return 1 - (1 - t) * (1 - t);
        case Ease::InOutQuad: return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
        case Ease::OutBack: {
            constexpr f32 c1 = 1.70158f, c3 = c1 + 1;
            f32 u = t - 1;
            return 1 + c3 * u * u * u + c1 * u * u;
        }
        case Ease::OutElastic: {
            if (t == 0 || t == 1) return t;
            constexpr f32 c4 = 2 * kPi / 3;
            return std::pow(2.f, -10 * t) * std::sin((t * 10 - 0.75f) * c4) + 1;
        }
        case Ease::OutBounce: {
            constexpr f32 n1 = 7.5625f, d1 = 2.75f;
            if (t < 1 / d1) return n1 * t * t;
            if (t < 2 / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
            if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
            t -= 2.625f / d1;
            return n1 * t * t + 0.984375f;
        }
    }
    return t;
}

namespace {

void apply(World& world, Entity e, Transform& t, TweenProp prop, Vec2 v) {
    switch (prop) {
        case TweenProp::Position:
            t.pos = t.prevPos = v; // tweened motion is per-frame; don't interpolate it
            break;
        case TweenProp::Scale:
            t.scale = v;
            break;
        case TweenProp::Rotation:
            t.rotation = t.prevRotation = v.x;
            break;
        case TweenProp::Alpha:
            if (Sprite* s = world.tryGet<Sprite>(e)) s->color.a = u8(std::lround(clamp(v.x, 0.f, 1.f) * 255));
            break;
    }
}

i32 totalLegs(const Tween& tw) {
    if (tw.repeats < 0 && tw.loop != TweenLoop::None) return -1;
    switch (tw.loop) {
        case TweenLoop::None: return 1;
        case TweenLoop::Loop: return tw.repeats < 1 ? 1 : tw.repeats;
        case TweenLoop::PingPong: return 2 * (tw.repeats < 1 ? 1 : tw.repeats);
    }
    return 1;
}

} // namespace

void updateTweens(World& world, f32 dt) {
    for (auto [e, tw] : world.view<Tween>()) {
        Transform* t = world.tryGet<Transform>(e);
        if (!t) continue;
        tw.elapsed += dt;
        const i32 legs = totalLegs(tw);
        bool finished = false;
        while (tw.elapsed >= tw.duration) {
            ++tw.legsDone;
            if (legs > 0 && tw.legsDone >= legs) {
                finished = true;
                break;
            }
            tw.elapsed -= tw.duration;
        }

        bool reverse = tw.loop == TweenLoop::PingPong && (tw.legsDone % 2 == 1);
        Vec2 value;
        if (finished) {
            // A ping-pong ends where it started; everything else ends at `to`.
            value = tw.loop == TweenLoop::PingPong ? tw.from : tw.to;
        } else {
            f32 k = ease(tw.easing, tw.elapsed / tw.duration);
            value = reverse ? lerp(tw.to, tw.from, k) : lerp(tw.from, tw.to, k);
        }
        apply(world, e, *t, tw.prop, value);

        // Removing the current element while iterating a single-component view is safe in EnTT.
        if (finished && tw.removeOnComplete) world.remove<Tween>(e);
    }
}

} // namespace nova
