#pragma once

#include <nova/physics/body_def.hpp>

#include <span>
#include <vector>

namespace nova {

// Box2D world in design-pixel units (1 m = 100 px). Knows nothing about the ECS:
// bodies carry a u32 of user data, and contacts report it back.
class PhysicsWorld {
public:
    using BodyId = u64; // packed b2BodyId; 0 is never a valid body
    static constexpr f32 kPixelsPerMeter = 100.f;

    struct RawContact {
        u32 userA;
        u32 userB;
        Vec2 normal; // from A toward B; {0,0} for sensor hits
        bool sensor;
    };

    explicit PhysicsWorld(Vec2 gravityPx = {0, 980});
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    BodyId createBody(const BodyDef& def, Vec2 posPx, f32 rotation, u32 userData);
    void destroyBody(BodyId body);
    // Advances the simulation and rebuilds contacts() from this step's begin events.
    void step(f32 dt);

    Vec2 position(BodyId body) const;
    f32 rotation(BodyId body) const;
    Vec2 velocity(BodyId body) const;
    void setVelocity(BodyId body, Vec2 velocityPx);
    void setTransform(BodyId body, Vec2 posPx, f32 rotation);
    // Kinematic bodies: sets the velocity that reaches posPx after a step of dt.
    void setTargetPosition(BodyId body, Vec2 posPx, f32 dt);
    void applyImpulse(BodyId body, Vec2 impulse);
    void setGravity(Vec2 gravityPx);
    bool isStatic(BodyId body) const;

    std::span<const RawContact> contacts() const { return contacts_; }
    u32 bodyCount() const;

private:
    u32 worldId_; // packed b2WorldId
    std::vector<RawContact> contacts_;
};

} // namespace nova
