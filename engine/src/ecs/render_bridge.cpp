#include <nova/ecs/render_bridge.hpp>
#include <nova/ecs/world.hpp>
#include <nova/render/sprite_batch.hpp>

namespace nova {

void collectSprites(World& world, SpriteBatch& batch, f32 alpha, Vec2 viewOrigin) {
    u16 order = 0;
    for (auto [e, t, s] : world.view<Transform, Sprite>()) {
        if (!s.visible || world.pendingDestroy(e)) continue;
        Vec2 pos = lerp(t.prevPos, t.pos, alpha) - viewOrigin;
        f32 rot = lerp(t.prevRotation, t.rotation, alpha);
        batch.push({makeSortKey(s.layer, s.blend, u16(s.region.texture.index), order++), s.region.texture,
                    s.region.src, Affine2::trs(pos, rot, t.scale), s.size, s.pivot, s.color, s.blend});
    }
}

} // namespace nova
