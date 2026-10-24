#include "ui/animation.hpp"

#include <nova/core/log.hpp>

#include <algorithm>

namespace nova::ui {
namespace {

Vec2 current(const Element& e, u8 property) {
    switch (property) {
        case Anim::Scale: return {e.scale, 0};
        case Anim::Opacity: return {e.spec.opacity, 0};
        default: return e.spec.offset;
    }
}

void apply(Element& e, u8 property, Vec2 v) {
    switch (property) {
        case Anim::Scale: e.scale = v.x; break;
        case Anim::Opacity: e.spec.opacity = v.x; break;
        default: e.spec.offset = v; break;
    }
}

} // namespace

Animator& Animator::start(u8 property, Vec2 to, f32 seconds) {
    slot_ = -1;
    if (!canvas_) return *this;
    Canvas::Impl& impl = canvas_->impl();
    Element* e = impl.store.live(h_);
    if (!e) return *this;
    // Reuse this element's running track of the same kind, else a free slot, else the oldest.
    i32 pick = -1, oldest = -1;
    for (i32 i = 0; i < i32(kAnimSlots); ++i) {
        const Anim& a = impl.anims[size_t(i)];
        if (a.active && a.h == h_ && a.property == property) {
            pick = i;
            break;
        }
        if (!a.active && pick < 0) pick = i;
        if (oldest < 0 || a.started < impl.anims[size_t(oldest)].started) oldest = i;
    }
    if (pick < 0) {
        pick = oldest;
#if defined(NOVA_DEV_TOOLS)
        static bool warned = false;
        if (!warned) log::warn("ui: all %zu animation slots busy; reusing the oldest", kAnimSlots);
        warned = true;
#endif
    }
    Anim& a = impl.anims[size_t(pick)];
    a = Anim{};
    a.h = h_;
    a.property = property;
    a.from = current(*e, property);
    a.to = to;
    a.duration = std::max(seconds, 0.f);
    a.active = true;
    a.started = ++impl.animCounter;
    slot_ = pick;
    return *this;
}

Animator& Animator::scale(f32 to, f32 seconds) { return start(Anim::Scale, {to, 0}, seconds); }
Animator& Animator::fade(f32 to, f32 seconds) { return start(Anim::Opacity, {to, 0}, seconds); }
Animator& Animator::move(Vec2 offset, f32 seconds) { return start(Anim::Offset, offset, seconds); }

#define NOVA_ANIM_SLOT()                                                   \
    if (!canvas_ || slot_ < 0) return *this;                               \
    Anim& a = canvas_->impl().anims[size_t(slot_)];                       \
    if (!a.active || !(a.h == h_)) return *this

Animator& Animator::ease(Ease e) {
    NOVA_ANIM_SLOT();
    a.ease = e;
    return *this;
}
Animator& Animator::delay(f32 seconds) {
    NOVA_ANIM_SLOT();
    a.delay = std::max(seconds, 0.f);
    return *this;
}
Animator& Animator::yoyo() {
    NOVA_ANIM_SLOT();
    a.yoyo = true;
    return *this;
}
Animator& Animator::loop() {
    NOVA_ANIM_SLOT();
    a.loop = true;
    return *this;
}
Animator& Animator::onDone(std::function<void()> fn) {
    NOVA_ANIM_SLOT();
    a.onDone = std::move(fn);
    return *this;
}
#undef NOVA_ANIM_SLOT

void updateAnimations(Canvas::Impl& impl, f32 dt) {
    for (Anim& a : impl.anims) {
        if (!a.active) continue;
        Element* e = impl.store.live(a.h);
        if (!e) {
            a = Anim{}; // element gone: the track ends silently
            continue;
        }
        f32 step = dt;
        if (a.delay > 0) {
            const f32 used = std::min(a.delay, step);
            a.delay -= used;
            step -= used;
        }
        a.elapsed += step;
        bool finished = false;
        if (a.duration <= 0 || a.elapsed >= a.duration) {
            a.elapsed = a.duration;
            if (a.yoyo && !a.backward) {
                a.backward = true;
                a.elapsed = 0;
            } else if (a.loop) {
                a.backward = false;
                a.elapsed = 0;
            } else {
                finished = true;
            }
        }
        f32 t = a.duration > 0 ? a.elapsed / a.duration : 1;
        if (finished) t = 1;
        f32 k = ease(a.ease, t);
        if (a.backward && !finished) k = 1 - k;
        if (finished && a.yoyo) k = 0; // a finished yoyo ends where it started
        apply(*e, a.property, a.from + (a.to - a.from) * k);
        if (finished) {
            std::function<void()> done = std::move(a.onDone);
            a = Anim{};
            if (done) done();
        }
    }

    // Progress bars ease toward their value; pressed widgets ease their scale.
    impl.store.forEach([dt](UiHandle, Element& e) {
        if (e.kind == Kind::ProgressBar && e.shownT < 1) {
            e.shownT = e.spec.animate > 0 ? std::min(1.f, e.shownT + dt / e.spec.animate) : 1;
            e.shown = e.shownFrom + (e.spec.value - e.shownFrom) * ease(Ease::OutQuad, e.shownT);
        }
        const f32 target = e.pressed ? 1.f : 0.f;
        if (e.pressAmount != target) {
            const f32 d = dt / kPressSeconds;
            e.pressAmount = target > e.pressAmount ? std::min(target, e.pressAmount + d) : std::max(target, e.pressAmount - d);
        }
    });
}

} // namespace nova::ui
