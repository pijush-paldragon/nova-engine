#include <nova/physics/physics_world.hpp>

#include <box2d/box2d.h>

#include <cstdint>
#include <cstring>

namespace nova {
namespace {

static_assert(sizeof(b2BodyId) <= sizeof(PhysicsWorld::BodyId));
static_assert(sizeof(b2WorldId) <= sizeof(u32));

constexpr f32 kToMeters = 1.0f / PhysicsWorld::kPixelsPerMeter;
constexpr int kSubSteps = 4;

b2Vec2 toMeters(Vec2 px) { return {px.x * kToMeters, px.y * kToMeters}; }
Vec2 toPixels(b2Vec2 m) { return {m.x * PhysicsWorld::kPixelsPerMeter, m.y * PhysicsWorld::kPixelsPerMeter}; }

PhysicsWorld::BodyId pack(b2BodyId id) {
    PhysicsWorld::BodyId out = 0;
    std::memcpy(&out, &id, sizeof id);
    return out;
}
b2BodyId unpack(PhysicsWorld::BodyId packed) {
    b2BodyId id;
    std::memcpy(&id, &packed, sizeof id);
    return id;
}
b2WorldId unpackWorld(u32 packed) {
    b2WorldId id;
    std::memcpy(&id, &packed, sizeof id);
    return id;
}
u32 userOf(b2ShapeId shape) {
    return u32(reinterpret_cast<std::uintptr_t>(b2Body_GetUserData(b2Shape_GetBody(shape))));
}

} // namespace

PhysicsWorld::PhysicsWorld(Vec2 gravityPx) : worldId_(0) {
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = toMeters(gravityPx);
    b2WorldId id = b2CreateWorld(&def);
    std::memcpy(&worldId_, &id, sizeof id);
    contacts_.reserve(256);
}

PhysicsWorld::~PhysicsWorld() { b2DestroyWorld(unpackWorld(worldId_)); }

PhysicsWorld::BodyId PhysicsWorld::createBody(const BodyDef& def, Vec2 posPx, f32 rotation, u32 userData) {
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = def.type == BodyType::Dynamic ? b2_dynamicBody
              : def.type == BodyType::Kinematic ? b2_kinematicBody
                                                : b2_staticBody;
    bd.position = b2ToPos(toMeters(posPx));
    bd.rotation = b2MakeRot(rotation);
    bd.isBullet = def.isBullet;
    bd.motionLocks.angularZ = def.lockRotation;
    bd.userData = reinterpret_cast<void*>(std::uintptr_t(userData));
    b2BodyId body = b2CreateBody(unpackWorld(worldId_), &bd);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = def.densityValue;
    sd.material.friction = def.frictionValue;
    sd.material.restitution = def.restitutionValue;
    sd.isSensor = def.isSensor;
    sd.enableSensorEvents = true;
    sd.enableContactEvents = !def.isSensor;
    if (def.shape == ShapeType::Circle) {
        b2Circle circle{{0, 0}, def.radius * kToMeters};
        b2CreateCircleShape(body, &sd, &circle);
    } else {
        b2Polygon box = b2MakeBox(def.boxSize.x * 0.5f * kToMeters, def.boxSize.y * 0.5f * kToMeters);
        b2CreatePolygonShape(body, &sd, &box);
    }
    return pack(body);
}

void PhysicsWorld::destroyBody(BodyId body) {
    b2BodyId id = unpack(body);
    if (body != 0 && b2Body_IsValid(id)) b2DestroyBody(id);
}

void PhysicsWorld::step(f32 dt) {
    b2WorldId world = unpackWorld(worldId_);
    b2World_Step(world, dt, kSubSteps);
    contacts_.clear();

    b2ContactEvents events = b2World_GetContactEvents(world);
    for (int i = 0; i < events.beginCount; ++i) {
        const b2ContactBeginTouchEvent& ev = events.beginEvents[i];
        if (!b2Shape_IsValid(ev.shapeIdA) || !b2Shape_IsValid(ev.shapeIdB)) continue;
        Vec2 normal{};
        if (b2Contact_IsValid(ev.contactId)) {
            b2ContactData data = b2Contact_GetData(ev.contactId);
            normal = {data.manifold.normal.x, data.manifold.normal.y};
        }
        contacts_.push_back({userOf(ev.shapeIdA), userOf(ev.shapeIdB), normal, false});
    }

    b2SensorEvents sensors = b2World_GetSensorEvents(world);
    for (int i = 0; i < sensors.beginCount; ++i) {
        const b2SensorBeginTouchEvent& ev = sensors.beginEvents[i];
        if (!b2Shape_IsValid(ev.sensorShapeId) || !b2Shape_IsValid(ev.visitorShapeId)) continue;
        contacts_.push_back({userOf(ev.sensorShapeId), userOf(ev.visitorShapeId), {}, true});
    }
}

Vec2 PhysicsWorld::position(BodyId body) const { return toPixels(b2ToVec2(b2Body_GetPosition(unpack(body)))); }
f32 PhysicsWorld::rotation(BodyId body) const { return b2Rot_GetAngle(b2Body_GetRotation(unpack(body))); }
Vec2 PhysicsWorld::velocity(BodyId body) const { return toPixels(b2Body_GetLinearVelocity(unpack(body))); }

void PhysicsWorld::setVelocity(BodyId body, Vec2 velocityPx) {
    b2Body_SetLinearVelocity(unpack(body), toMeters(velocityPx));
}

void PhysicsWorld::setTransform(BodyId body, Vec2 posPx, f32 rot) {
    b2Body_SetTransform(unpack(body), b2ToPos(toMeters(posPx)), b2MakeRot(rot));
}

void PhysicsWorld::setTargetPosition(BodyId body, Vec2 posPx, f32 dt) {
    b2BodyId id = unpack(body);
    b2WorldTransform target;
    target.p = b2ToPos(toMeters(posPx));
    target.q = b2Body_GetRotation(id);
    b2Body_SetTargetTransform(id, target, dt, true);
}

void PhysicsWorld::applyImpulse(BodyId body, Vec2 impulse) {
    b2Body_ApplyLinearImpulseToCenter(unpack(body), toMeters(impulse), true);
}

void PhysicsWorld::setGravity(Vec2 gravityPx) { b2World_SetGravity(unpackWorld(worldId_), toMeters(gravityPx)); }

bool PhysicsWorld::isStatic(BodyId body) const { return b2Body_GetType(unpack(body)) == b2_staticBody; }

u32 PhysicsWorld::bodyCount() const { return u32(b2World_GetCounters(unpackWorld(worldId_)).bodyCount); }

} // namespace nova
