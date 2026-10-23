#include <nova/core/log.hpp>
#include <nova/ecs/physics.hpp>

namespace nova {

Physics::Physics(World& world, Vec2 gravityPx) : world_(world), physics_(gravityPx) {
    hook_ = world_.onDestroy<RigidBody>(&Physics::onBodyDestroyed, this);
    contacts_.reserve(256);
}

Physics::~Physics() {
    // Disconnect first: the World may outlive us, and its RigidBody components must not call back.
    world_.removeHook(hook_);
}

void Physics::onBodyDestroyed(void* self, World& world, Entity e) {
    auto* physics = static_cast<Physics*>(self);
    if (RigidBody* rb = world.registry().try_get<RigidBody>(e)) physics->physics_.destroyBody(rb->id);
}

void Physics::attach(Entity e, const BodyDef& def) {
    Transform* t = world_.tryGet<Transform>(e);
    if (!t) {
        log::error("Physics::attach: entity %u has no Transform", u32(e));
        return;
    }
    if (world_.has<RigidBody>(e)) world_.remove<RigidBody>(e); // replacing destroys the old body
    world_.add<RigidBody>(e, RigidBody{physics_.createBody(def, t->pos, t->rotation, u32(e))});
}

PhysicsWorld::BodyId Physics::bodyOf(Entity e) const {
    const RigidBody* rb = const_cast<World&>(world_).tryGet<RigidBody>(e);
    return rb ? rb->id : 0;
}

Vec2 Physics::velocity(Entity e) const {
    auto id = bodyOf(e);
    return id ? physics_.velocity(id) : Vec2{};
}
void Physics::setVelocity(Entity e, Vec2 v) {
    if (auto id = bodyOf(e)) physics_.setVelocity(id, v);
}
void Physics::setTargetPosition(Entity e, Vec2 pos) {
    if (auto id = bodyOf(e)) physics_.setTargetPosition(id, pos, lastDt_);
}
void Physics::setPosition(Entity e, Vec2 pos) {
    auto id = bodyOf(e);
    if (!id) return;
    Transform* t = world_.tryGet<Transform>(e);
    physics_.setTransform(id, pos, t ? t->rotation : 0);
    if (t) t->pos = t->prevPos = pos; // teleports don't interpolate
}
void Physics::applyImpulse(Entity e, Vec2 impulse) {
    if (auto id = bodyOf(e)) physics_.applyImpulse(id, impulse);
}

void Physics::step(f32 dt) {
    lastDt_ = dt;
    physics_.step(dt);
    for (const auto& raw : physics_.contacts()) {
        Entity a = Entity(raw.userA), b = Entity(raw.userB);
        if (!world_.alive(a) || !world_.alive(b) || world_.pendingDestroy(a) || world_.pendingDestroy(b)) continue;
        contacts_.push_back({a, b, raw.normal, raw.sensor});
    }
}

void Physics::syncTransforms() {
    for (auto [e, rb, t] : world_.view<RigidBody, Transform>()) {
        if (physics_.isStatic(rb.id)) continue;
        t.pos = physics_.position(rb.id);
        t.rotation = physics_.rotation(rb.id);
    }
}

} // namespace nova
