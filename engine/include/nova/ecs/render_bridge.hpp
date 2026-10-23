#pragma once

#include <nova/core/math.hpp>

namespace nova {

class World;
class SpriteBatch;

// Pushes every visible Sprite into the batch at its interpolated transform.
// alpha: fraction of the way from the previous fixed step to the current one.
// viewOrigin: world point drawn at the design-area origin (camera position + shake).
void collectSprites(World& world, SpriteBatch& batch, f32 alpha, Vec2 viewOrigin);

} // namespace nova
