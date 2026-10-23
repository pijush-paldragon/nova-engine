#include <doctest/doctest.h>
#include <nova/core/random.hpp>
#include <nova/ecs/particles.hpp>
#include <nova/ecs/tween.hpp>
#include <nova/ecs/world.hpp>
#include <nova/render/sprite_batch.hpp>

using namespace nova;

TEST_CASE("ease endpoints and overshoot") {
    for (auto e : {Ease::Linear, Ease::InQuad, Ease::OutQuad, Ease::InOutQuad, Ease::OutBack, Ease::OutElastic,
                   Ease::OutBounce}) {
        CHECK(ease(e, 0.f) == doctest::Approx(0));
        CHECK(ease(e, 1.f) == doctest::Approx(1));
        CHECK(ease(e, 2.f) == doctest::Approx(1)); // clamped
    }
    CHECK(ease(Ease::OutBack, 0.7f) > 1.f);
    CHECK(ease(Ease::InQuad, 0.5f) == doctest::Approx(0.25f));
}

TEST_CASE("Tween reaches target and removes itself") {
    World w;
    auto e = w.spawn();
    w.add<Transform>(e);
    w.add<Tween>(e, Tween::scale({1, 1}, {2, 2}, 0.5f, Ease::Linear));
    updateTweens(w, 0.25f);
    CHECK(w.get<Transform>(e).scale.x == doctest::Approx(1.5f));
    updateTweens(w, 0.3f);
    CHECK(w.get<Transform>(e).scale.x == doctest::Approx(2));
    CHECK(!w.has<Tween>(e));
}

TEST_CASE("Tween ping-pong once returns to start") {
    World w;
    auto e = w.spawn();
    w.add<Transform>(e);
    w.add<Tween>(e, Tween::scale({1, 1}, {2, 2}, 0.5f, Ease::Linear).pingPong(1));
    updateTweens(w, 0.5f);
    CHECK(w.get<Transform>(e).scale.x == doctest::Approx(2));
    CHECK(w.has<Tween>(e));
    updateTweens(w, 0.25f);
    CHECK(w.get<Transform>(e).scale.x == doctest::Approx(1.5f));
    updateTweens(w, 0.25f);
    CHECK(w.get<Transform>(e).scale.x == doctest::Approx(1));
    CHECK(!w.has<Tween>(e));
}

TEST_CASE("Tween alpha drives sprite colour and position tween skips interpolation") {
    World w;
    auto e = w.spawn();
    w.add<Transform>(e);
    w.add<Sprite>(e);
    w.add<Tween>(e, Tween::alpha(1, 0, 1.f, Ease::Linear).looping(-1));
    updateTweens(w, 0.5f);
    CHECK(int(w.get<Sprite>(e).color.a) == doctest::Approx(128).epsilon(0.01));
    updateTweens(w, 1.0f); // looping forever: still present
    CHECK(w.has<Tween>(e));

    auto m = w.spawn();
    w.add<Transform>(m);
    w.add<Tween>(m, Tween::position({0, 0}, {100, 0}, 1.f, Ease::Linear));
    updateTweens(w, 0.5f);
    CHECK(w.get<Transform>(m).pos.x == doctest::Approx(50));
    CHECK(w.get<Transform>(m).prevPos.x == doctest::Approx(50));
}

TEST_CASE("Particles burst, cap at capacity, die, and self-destroy") {
    World w;
    Random r{1};
    auto e = w.spawn();
    w.add<Transform>(e, Transform{.pos{100, 100}});
    auto& em = w.add<ParticleEmitter>(e, makeEmitter({.lifetime{0.5f, 0.5f}}, 32));
    em.destroyWhenDone = true;
    em.burst(50);
    updateParticles(w, 0.f, r);
    CHECK(w.get<ParticleEmitter>(e).alive() == 32);
    SpriteBatch b;
    b.begin();
    collectParticles(w, b, {0, 0});
    CHECK(b.spriteCount() == 32);
    updateParticles(w, 0.6f, r);
    w.flushDestroyed();
    CHECK(!w.alive(e));
}

TEST_CASE("Particles continuous rate emits over time") {
    World w;
    Random r{2};
    auto e = w.spawn();
    w.add<Transform>(e);
    w.add<ParticleEmitter>(e, makeEmitter({.rate = 100, .lifetime{10, 10}}, 64));
    updateParticles(w, 0.1f, r);
    CHECK(w.get<ParticleEmitter>(e).alive() == 10);
    updateParticles(w, 1.f, r);
    CHECK(w.get<ParticleEmitter>(e).alive() == 64); // capped
}

TEST_CASE("Tween with zero duration built by aggregate init does not hang") {
    World w;
    auto e = w.spawn();
    w.add<Transform>(e);
    Tween tw{.prop = TweenProp::Scale, .from{1, 1}, .to{3, 3}, .duration = 0};
    w.add<Tween>(e, tw.pingPong()); // forever: the leg loop must still terminate each update
    updateTweens(w, 0.016f);
    updateTweens(w, 0.016f);
    CHECK(w.has<Tween>(e));
    float s = w.get<Transform>(e).scale.x;
    CHECK(s >= 1.f);
    CHECK(s <= 3.f);
}
