#pragma once

#include <nova/core/types.hpp>

namespace nova {

class World;
class Input;
class Physics;
class Assets;
class Camera2D;
class FrameArena;
class Random;

enum class Phase : u8 {
    Fixed,  // 60 Hz simulation step, after physics
    Update, // once per rendered frame
};

// Everything a system may touch. Every reference is bound to a live object.
struct SystemContext {
    World& world;
    Input& input;
    Physics& physics;
    Assets& assets;
    Camera2D& camera;
    FrameArena& frame;
    Random& random;
    f32 dt;
};

// Systems are plain functions (capture-less lambdas convert implicitly).
using SystemFn = void (*)(SystemContext&);

} // namespace nova
