#pragma once

#include <nova/input/input.hpp>
#include <nova/render/camera.hpp>

union SDL_Event;

namespace nova {

// Engine-side input: turns SDL events into the per-frame snapshot.
class InputBackend final : public Input {
public:
    InputBackend() = default;

    // Ends the previous frame's snapshot: drops released pointers and clears one-frame flags.
    // Events for the next frame are fed through handleEvent afterwards.
    void beginFrame(f32 dt);
    void handleEvent(const SDL_Event& event, const ScreenFit& fit);

private:
    Pointer* find(u64 id);
    void press(u64 id, Vec2 pos);
    void move(u64 id, Vec2 pos, Vec2 delta);
    void release(u64 id, Vec2 pos, bool canTap);
    void setKey(Key k, bool down);
};

} // namespace nova
