#pragma once

#include <nova/ecs/world.hpp>
#include <nova/physics/physics_world.hpp>

#include <span>
#include <vector>

namespace nova {

struct RigidBody {
    PhysicsWorld::BodyId id = 0;
};

struct Contact {
    Entity a;
    Entity b;
    Vec2 normal; // from a toward b; {0,0} for sensor hits
    bool sensor; // for sensor hits, a is the sensor
};

// Game-facing physics for one scene: attaches Box2D bodies to entities, keeps Transform in
// sync, and reports contacts by entity. Destroying an entity (or removing its RigidBody)
// destroys its body.
class Physics {
public:
    explicit Physics(World& world, Vec2 gravityPx = {0, 980});
    ~Physics();
    Physics(const Physics&) = delete;
    Physics& operator=(const Physics&) = delete;

    // Creates a body at the entity's Transform. The entity must have a Transform.
    void attach(Entity e, const BodyDef& def);

    Vec2 velocity(Entity e) const;
    void setVelocity(Entity e, Vec2 velocityPx);
    void setTargetPosition(Entity e, Vec2 posPx); // kinematic bodies
    void setPosition(Entity e, Vec2 posPx);       // teleport
    void applyImpulse(Entity e, Vec2 impulse);
    void setGravity(Vec2 gravityPx) { physics_.setGravity(gravityPx); }

    // Every contact that began this frame (across all fixed steps), excluding entities
    // already pending destruction.
    std::span<const Contact> contacts() const { return contacts_; }

    // Engine: clear contacts at frame start; step; copy bodies into Transforms.
    void beginFrame() { contacts_.clear(); }
    void step(f32 dt);
    void syncTransforms();

    PhysicsWorld& physicsWorld() { return physics_; }

private:
    PhysicsWorld::BodyId bodyOf(Entity e) const;
    static void onBodyDestroyed(void* self, World& world, Entity e);

    World& world_;
    PhysicsWorld physics_;
    World::HookId hook_;
    std::vector<Contact> contacts_;
    f32 lastDt_ = 1.f / 60;
};

} // namespace nova
