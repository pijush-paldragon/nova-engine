#pragma once

#include <nova/core/math.hpp>
#include <nova/render/types.hpp>

#include <string>

namespace nova {

// Position in world units (design pixels), y-down. prevPos/prevRotation are maintained by
// the engine for render interpolation between fixed steps; game code never touches them.
struct Transform {
    Vec2 pos{};
    f32 rotation = 0; // radians
    Vec2 scale{1, 1};
    Vec2 prevPos{};
    f32 prevRotation = 0;
};

struct Sprite {
    SpriteRegion region;
    Vec2 size{};
    Vec2 pivot{0.5f, 0.5f};
    Color color = Color::White;
    u8 layer = 0;
    BlendMode blend = BlendMode::Alpha;
    bool visible = true;
};

struct Name {
    std::string value;
};

} // namespace nova
