#include <doctest/doctest.h>
#include <nova/ecs/physics.hpp>
#include <nova/ecs/world.hpp>

#include <cmath>

using namespace nova;

namespace {
void stepFrames(World& w, Physics& p, int frames) {
    for (int i = 0; i < frames; ++i) {
        w.snapshotTransforms();
        p.step(1.f / 60);
        p.syncTransforms();
    }
}
} // namespace

TEST_CASE("Physics dynamic body falls under gravity and syncs Transform") {
    World w;
    Physics p{w};
    auto e = w.spawn();
    w.add<Transform>(e, Transform{.pos{100, 100}});
    p.attach(e, BodyDef::dynamic().circle(10));
    CHECK(w.has<RigidBody>(e));
    stepFrames(w, p, 60);
    CHECK(w.get<Transform>(e).pos.y > 500); // ~ 100 + 0.5 * 980 * 1^2
    CHECK(w.get<Transform>(e).pos.x == doctest::Approx(100));
    CHECK(w.get<Transform>(e).prevPos.y < w.get<Transform>(e).pos.y);
    CHECK(p.velocity(e).y > 900);
}

TEST_CASE("Physics reports contacts and sensor hits with entities") {
    World w;
    Physics p{w, {0, 0}};
    auto ball = w.spawn();
    w.add<Transform>(ball, Transform{.pos{100, 100}});
    p.attach(ball, BodyDef::dynamic().circle(10).restitution(1).friction(0).bullet());
    p.setVelocity(ball, {0, 600});
    auto wall = w.spawn();
    w.add<Transform>(wall, Transform{.pos{100, 200}});
    p.attach(wall, BodyDef::staticBody().box({200, 20}));
    auto zone = w.spawn();
    w.add<Transform>(zone, Transform{.pos{100, 30}});
    p.attach(zone, BodyDef::staticBody().box({200, 20}).sensor());

    int solid = 0, sensor = 0;
    for (int i = 0; i < 90; ++i) {
        p.beginFrame();
        w.snapshotTransforms();
        p.step(1.f / 60);
        p.syncTransforms();
        for (const Contact& c : p.contacts()) {
            bool involvesBall = c.a == ball || c.b == ball;
            CHECK(involvesBall);
            if (c.sensor) {
                ++sensor;
                CHECK((c.a == zone || c.b == zone));
            } else {
                ++solid;
                CHECK((c.a == wall || c.b == wall));
                CHECK(std::abs(c.normal.y) == doctest::Approx(1).epsilon(0.01));
            }
        }
    }
    CHECK(solid == 1); // bounced off the wall once...
    CHECK(sensor == 1); // ...then flew up into the sensor
    CHECK(p.velocity(ball).y < 0);
}

TEST_CASE("Physics contacts skip entities pending destruction") {
    World w;
    Physics p{w, {0, 0}};
    auto ball = w.spawn();
    w.add<Transform>(ball, Transform{.pos{100, 100}});
    p.attach(ball, BodyDef::dynamic().circle(10));
    p.setVelocity(ball, {0, 600});
    auto wall = w.spawn();
    w.add<Transform>(wall, Transform{.pos{100, 140}});
    p.attach(wall, BodyDef::staticBody().box({200, 20}));
    w.destroy(wall); // still in the physics world until flush
    int seen = 0;
    for (int i = 0; i < 20; ++i) {
        p.beginFrame();
        p.step(1.f / 60);
        seen += int(p.contacts().size());
    }
    CHECK(seen == 0);
}

TEST_CASE("Physics destroys body when entity is destroyed") {
    World w;
    Physics p{w};
    auto e = w.spawn();
    w.add<Transform>(e);
    p.attach(e, BodyDef::staticBody().box({10, 10}));
    CHECK(p.physicsWorld().bodyCount() == 1);
    w.destroy(e);
    w.flushDestroyed();
    CHECK(p.physicsWorld().bodyCount() == 0);
}

TEST_CASE("Physics destroyed before World is safe") {
    World w;
    {
        Physics p{w};
        auto e = w.spawn();
        w.add<Transform>(e);
        p.attach(e, BodyDef::dynamic().circle(5));
    }
    w.flushDestroyed(); // World (and its RigidBody storage) outlives Physics; ASan must stay silent
}

TEST_CASE("Physics kinematic target is reached in one step") {
    World w;
    Physics p{w};
    auto e = w.spawn();
    w.add<Transform>(e, Transform{.pos{0, 0}});
    p.attach(e, BodyDef::kinematic().box({100, 20}));
    p.setTargetPosition(e, {60, 0});
    p.step(1.f / 60);
    p.syncTransforms();
    CHECK(w.get<Transform>(e).pos.x == doctest::Approx(60).epsilon(0.01));
    CHECK(w.get<Transform>(e).pos.y == doctest::Approx(0));
}

TEST_CASE("Physics setPosition teleports and attach without Transform is refused") {
    World w;
    Physics p{w};
    auto e = w.spawn();
    w.add<Transform>(e);
    p.attach(e, BodyDef::dynamic().circle(5));
    p.setPosition(e, {300, 400});
    p.syncTransforms();
    CHECK(w.get<Transform>(e).pos == Vec2{300, 400});
    auto bare = w.spawn();
    p.attach(bare, BodyDef::dynamic().circle(5)); // logs an error, no body
    CHECK(!w.has<RigidBody>(bare));
    CHECK(p.physicsWorld().bodyCount() == 1);
}
