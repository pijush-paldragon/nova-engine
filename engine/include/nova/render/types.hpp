#pragma once

#include <nova/core/handle.hpp>
#include <nova/core/math.hpp>

namespace nova {

struct TextureTag;
using TextureHandle = Handle<TextureTag>;

enum class BlendMode : u8 { Alpha, Additive };

// A texture plus the source rectangle (in texels) to draw from it.
// A zero-width src means "the whole texture".
struct SpriteRegion {
    TextureHandle texture;
    Rect src;
};

} // namespace nova
