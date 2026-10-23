#pragma once

#include <nova/core/math.hpp>
#include <nova/render/types.hpp>

#include <memory>

namespace nova {

class World;
class SpriteBatch;
class Random;

// Ranges are {min, max}; colours and sizes interpolate from start to end over each lifetime.
struct EmitterConfig {
    SpriteRegion region;
    f32 rate = 0;                 // particles per second (0 = bursts only)
    Vec2 lifetime{0.4f, 0.8f};    // seconds
    Vec2 speed{100, 300};         // design px per second
    Vec2 angleDeg{0, 360};        // emission direction; 0 = +x, 90 = down (y-down)
    Color startColor = Color::White;
    Color endColor = Color::White.withAlpha(0);
    Vec2 size{12, 4};             // start size, end size
    Vec2 gravity{0, 0};           // px per second^2
    u8 layer = 50;
    BlendMode blend = BlendMode::Additive;
};

// Fixed-capacity particle pool in structure-of-arrays layout, allocated once when created.
// Particles live in world space, spawned at the entity's Transform position.
struct ParticleEmitter {
    EmitterConfig config;
    bool destroyWhenDone = false; // destroy the entity once a non-continuous emitter is empty

    // Queues n particles; they spawn on the next updateParticles (capped at capacity).
    void burst(u32 n) { pendingBurst += n; }
    u32 alive() const { return count; }

    // Pool state (managed by updateParticles).
    std::unique_ptr<f32[]> data; // posX, posY, velX, velY, age, life (capacity floats each)
    u32 capacity = 0;
    u32 count = 0;
    u32 pendingBurst = 0;
    f32 rateAccumulator = 0;
};

ParticleEmitter makeEmitter(EmitterConfig config, u32 capacity);

void updateParticles(World& world, f32 dt, Random& random);
void collectParticles(World& world, SpriteBatch& batch, Vec2 viewOrigin);

} // namespace nova
