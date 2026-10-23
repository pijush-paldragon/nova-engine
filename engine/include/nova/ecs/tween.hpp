#pragma once

#include <nova/core/math.hpp>

namespace nova {

class World;

enum class Ease : u8 { Linear, InQuad, OutQuad, InOutQuad, OutBack, OutElastic, OutBounce };

// t is clamped to [0, 1]; every curve maps 0 -> 0 and 1 -> 1.
f32 ease(Ease e, f32 t);

enum class TweenProp : u8 { Position, Scale, Rotation, Alpha };
enum class TweenLoop : u8 { None, Loop, PingPong };

// Animates one property of its entity's Transform (or Sprite/TextLabel alpha). Because it is
// a component, it dies with its entity and can never write to freed memory.
// Scalar properties (rotation, alpha) use .x of from/to.
struct Tween {
    TweenProp prop = TweenProp::Scale;
    Vec2 from{};
    Vec2 to{};
    f32 duration = 1;
    f32 elapsed = 0;
    Ease easing = Ease::Linear;
    TweenLoop loop = TweenLoop::None;
    i32 repeats = 0; // Loop: total plays; PingPong: round trips; -1 = forever
    bool removeOnComplete = true;
    i32 legsDone = 0;

    static Tween position(Vec2 from, Vec2 to, f32 duration, Ease e) { return make(TweenProp::Position, from, to, duration, e); }
    static Tween scale(Vec2 from, Vec2 to, f32 duration, Ease e) { return make(TweenProp::Scale, from, to, duration, e); }
    static Tween rotation(f32 from, f32 to, f32 duration, Ease e) { return make(TweenProp::Rotation, {from, 0}, {to, 0}, duration, e); }
    static Tween alpha(f32 from, f32 to, f32 duration, Ease e) { return make(TweenProp::Alpha, {from, 0}, {to, 0}, duration, e); }

    Tween& pingPong(i32 roundTrips = -1) { loop = TweenLoop::PingPong; repeats = roundTrips; return *this; }
    Tween& looping(i32 plays = -1) { loop = TweenLoop::Loop; repeats = plays; return *this; }

private:
    static Tween make(TweenProp p, Vec2 from, Vec2 to, f32 duration, Ease e) {
        Tween t;
        t.prop = p;
        t.from = from;
        t.to = to;
        t.duration = duration > 0 ? duration : 1e-4f;
        t.easing = e;
        return t;
    }
};

void updateTweens(World& world, f32 dt);

} // namespace nova
