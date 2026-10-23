#pragma once

#include <nova/core/types.hpp>

#include <functional>
#include <vector>

namespace nova {

using TimerId = u32;

// Scene-owned timers. Cleared when the scene exits, so callbacks capturing the scene are safe.
// Timers added from inside a callback first fire on the next update; a timer cancelled from
// inside a callback does not fire again, even later in the same update.
class Timers {
public:
    TimerId after(f32 seconds, std::function<void()> fn);
    TimerId every(f32 seconds, std::function<void()> fn);
    void cancel(TimerId id);
    void update(f32 dt);
    void clear();
    size_t active() const;

private:
    struct Timer {
        TimerId id;
        f32 remaining;
        f32 interval; // > 0 for repeating timers
        std::function<void()> fn;
        bool dead = false;
    };

    TimerId add(f32 seconds, f32 interval, std::function<void()> fn);

    std::vector<Timer> timers_;
    std::vector<Timer> pending_;
    TimerId nextId_ = 1;
    bool updating_ = false;
};

} // namespace nova
