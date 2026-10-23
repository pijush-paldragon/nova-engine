#include <nova/core/random.hpp>
#include <nova/ecs/components.hpp>
#include <nova/ecs/particles.hpp>
#include <nova/ecs/world.hpp>
#include <nova/render/sprite_batch.hpp>

#include <cmath>

namespace nova {
namespace {

enum Field : u32 { PosX, PosY, VelX, VelY, Age, Life, FieldCount };

f32* field(ParticleEmitter& em, Field f) { return em.data.get() + size_t(f) * em.capacity; }

void spawn(ParticleEmitter& em, u32 n, Vec2 origin, Random& rng) {
    const EmitterConfig& c = em.config;
    for (u32 k = 0; k < n && em.count < em.capacity; ++k) {
        u32 i = em.count++;
        f32 angle = rng.range(c.angleDeg.x, c.angleDeg.y) * (kPi / 180.f);
        f32 speed = rng.range(c.speed.x, c.speed.y);
        field(em, PosX)[i] = origin.x;
        field(em, PosY)[i] = origin.y;
        field(em, VelX)[i] = std::cos(angle) * speed;
        field(em, VelY)[i] = std::sin(angle) * speed;
        field(em, Age)[i] = 0;
        field(em, Life)[i] = std::max(1e-3f, rng.range(c.lifetime.x, c.lifetime.y));
    }
}

} // namespace

ParticleEmitter makeEmitter(EmitterConfig config, u32 capacity) {
    ParticleEmitter em;
    em.config = config;
    em.capacity = capacity;
    em.data = std::make_unique<f32[]>(size_t(capacity) * FieldCount);
    return em;
}

void updateParticles(World& world, f32 dt, Random& rng) {
    for (auto [e, em] : world.view<ParticleEmitter>()) {
        if (!em.data) continue;
        const Transform* t = world.tryGet<Transform>(e);
        Vec2 origin = t ? t->pos : Vec2{};

        if (em.pendingBurst > 0) {
            spawn(em, em.pendingBurst, origin, rng);
            em.pendingBurst = 0;
        }
        if (em.config.rate > 0) {
            em.rateAccumulator += em.config.rate * dt;
            auto n = u32(em.rateAccumulator);
            em.rateAccumulator -= f32(n);
            spawn(em, n, origin, rng);
        }

        f32 *px = field(em, PosX), *py = field(em, PosY), *vx = field(em, VelX), *vy = field(em, VelY);
        f32 *age = field(em, Age), *life = field(em, Life);
        const Vec2 g = em.config.gravity;
        for (u32 i = 0; i < em.count;) {
            age[i] += dt;
            if (age[i] >= life[i]) { // swap-remove: order doesn't matter for additive particles
                u32 last = --em.count;
                px[i] = px[last]; py[i] = py[last]; vx[i] = vx[last]; vy[i] = vy[last];
                age[i] = age[last]; life[i] = life[last];
                continue;
            }
            vx[i] += g.x * dt;
            vy[i] += g.y * dt;
            px[i] += vx[i] * dt;
            py[i] += vy[i] * dt;
            ++i;
        }

        if (em.destroyWhenDone && em.count == 0 && em.config.rate <= 0 && em.pendingBurst == 0) world.destroy(e);
    }
}

void collectParticles(World& world, SpriteBatch& batch, Vec2 viewOrigin) {
    u16 order = 0;
    for (auto [e, em] : world.view<ParticleEmitter>()) {
        if (!em.data || em.count == 0 || world.pendingDestroy(e)) continue;
        const EmitterConfig& c = em.config;
        const f32 *px = field(em, PosX), *py = field(em, PosY), *age = field(em, Age), *life = field(em, Life);
        const u64 key = makeSortKey(c.layer, c.blend, u16(c.region.texture.index), order++);
        for (u32 i = 0; i < em.count; ++i) {
            f32 k = age[i] / life[i];
            f32 size = lerp(c.size.x, c.size.y, k);
            Vec2 pos = Vec2{px[i], py[i]} - viewOrigin;
            batch.push({key, c.region.texture, c.region.src, Affine2::trs(pos, 0, {1, 1}), {size, size},
                        {0.5f, 0.5f}, lerp(c.startColor, c.endColor, k), c.blend});
        }
    }
}

} // namespace nova
