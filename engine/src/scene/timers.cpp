#include <nova/scene/timers.hpp>

#include <algorithm>

namespace nova {

TimerId Timers::after(f32 seconds, std::function<void()> fn) { return add(seconds, 0, std::move(fn)); }

TimerId Timers::every(f32 seconds, std::function<void()> fn) {
    return add(seconds, seconds > 0 ? seconds : 1e-3f, std::move(fn));
}

TimerId Timers::add(f32 seconds, f32 interval, std::function<void()> fn) {
    TimerId id = nextId_++;
    (updating_ ? pending_ : timers_).push_back({id, seconds, interval, std::move(fn)});
    return id;
}

void Timers::cancel(TimerId id) {
    for (auto* list : {&timers_, &pending_}) {
        for (Timer& t : *list) {
            if (t.id == id) t.dead = true;
        }
    }
}

void Timers::update(f32 dt) {
    updating_ = true;
    // Index loop: callbacks only append to pending_, never to timers_, so references stay valid.
    for (size_t i = 0; i < timers_.size(); ++i) {
        Timer& t = timers_[i];
        if (t.dead) continue;
        t.remaining -= dt;
        while (t.remaining <= 0 && !t.dead) {
            t.fn();
            if (t.interval > 0) t.remaining += t.interval;
            else t.dead = true;
        }
    }
    updating_ = false;

    std::erase_if(timers_, [](const Timer& t) { return t.dead; });
    for (Timer& t : pending_) {
        if (!t.dead) timers_.push_back(std::move(t));
    }
    pending_.clear();
}

void Timers::clear() {
    if (updating_) {
        for (Timer& t : timers_) t.dead = true;
        for (Timer& t : pending_) t.dead = true;
        return;
    }
    timers_.clear();
    pending_.clear();
}

size_t Timers::active() const {
    size_t n = 0;
    for (const auto* list : {&timers_, &pending_}) {
        for (const Timer& t : *list) n += t.dead ? 0 : 1;
    }
    return n;
}

} // namespace nova
