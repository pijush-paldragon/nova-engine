#include <doctest/doctest.h>
#include <nova/core/alloc_stats.hpp>
#include <nova/ecs/components.hpp>
#include <nova/ecs/render_bridge.hpp>
#include <nova/ecs/world.hpp>
#include <nova/render/sprite_batch.hpp>

#include <vector>

using namespace nova;

namespace {
struct Marker {
    int value = 0;
};
} // namespace

TEST_CASE("World destroy is deferred, idempotent and safe inside view") {
    World w;
    std::vector<Entity> es;
    for (int i = 0; i < 5; ++i) {
        auto e = w.spawn();
        w.add<Transform>(e);
        es.push_back(e);
    }
    int visited = 0;
    for (auto [e, t] : w.view<Transform>()) {
        w.destroy(es[0]);
        w.destroy(es[0]);
        w.destroy(e);
        ++visited;
    }
    CHECK(visited == 5);
    CHECK(w.alive(es[0]));
    CHECK(w.pendingDestroy(es[0]));
    CHECK(w.count() == 5);
    w.flushDestroyed();
    CHECK(w.count() == 0);
    CHECK(!w.alive(es[0]));
    w.destroy(es[0]); // destroying a dead entity is a no-op
    w.flushDestroyed();
    CHECK(w.count() == 0);
}

TEST_CASE("World components add/get/tryGet/has/remove") {
    World w;
    auto e = w.spawn();
    auto& t = w.add<Transform>(e, Transform{.pos{5, 6}});
    CHECK(t.prevPos == Vec2{5, 6}); // adding a Transform seeds the interpolation state
    CHECK(w.has<Transform>(e));
    CHECK(w.get<Transform>(e).pos.x == 5);
    CHECK(w.tryGet<Marker>(e) == nullptr);
    w.add<Marker>(e, Marker{3});
    w.add<Marker>(e, Marker{4}); // replaces
    CHECK(w.get<Marker>(e).value == 4);
    w.remove<Marker>(e);
    CHECK(!w.has<Marker>(e));
    CHECK(!w.alive(kNullEntity));
}

TEST_CASE("World onDestroy hook fires once per removed component") {
    World w;
    int fired = 0;
    auto hook = w.onDestroy<Marker>([](void* user, World&, Entity) { ++*static_cast<int*>(user); }, &fired);
    auto a = w.spawn();
    auto b = w.spawn();
    w.add<Marker>(a);
    w.add<Marker>(b);
    w.destroy(a);
    w.flushDestroyed();
    CHECK(fired == 1);
    w.remove<Marker>(b);
    CHECK(fired == 2);
    w.removeHook(hook);
    auto c = w.spawn();
    w.add<Marker>(c);
    w.remove<Marker>(c);
    CHECK(fired == 2); // disconnected
}

TEST_CASE("World destruction fires onDestroy hooks for remaining components") {
    int fired = 0;
    {
        World w;
        w.onDestroy<Marker>([](void* user, World&, Entity) { ++*static_cast<int*>(user); }, &fired);
        for (int i = 0; i < 3; ++i) w.add<Marker>(w.spawn());
    }
    CHECK(fired == 3);
}

TEST_CASE("collectSprites interpolates between previous and current transform") {
    World w;
    auto e = w.spawn();
    auto& t = w.add<Transform>(e, Transform{.pos{0, 0}});
    w.add<Sprite>(e, Sprite{.region{{0, 1}, {0, 0, 10, 10}}, .size{10, 10}});
    w.snapshotTransforms();
    t.pos = {100, 0};
    SpriteBatch b;
    b.begin();
    collectSprites(w, b, 0.5f, {0, 0});
    b.build([](TextureHandle) { return Vec2{10, 10}; });
    REQUIRE(b.vertices().size() == 4);
    CHECK(b.vertices()[0].pos.x == doctest::Approx(50 - 5));
}

TEST_CASE("collectSprites skips invisible and pending-destroy sprites and applies view origin") {
    World w;
    auto visible = w.spawn();
    w.add<Transform>(visible, Transform{.pos{100, 100}});
    w.add<Sprite>(visible, Sprite{.size{10, 10}});
    auto hidden = w.spawn();
    w.add<Transform>(hidden);
    w.add<Sprite>(hidden, Sprite{.size{10, 10}, .visible = false});
    auto dying = w.spawn();
    w.add<Transform>(dying);
    w.add<Sprite>(dying, Sprite{.size{10, 10}});
    w.destroy(dying);
    SpriteBatch b;
    b.begin();
    collectSprites(w, b, 1.f, {40, 0});
    b.build([](TextureHandle) { return Vec2{10, 10}; });
    REQUIRE(b.vertices().size() == 4);
    CHECK(b.vertices()[0].pos.x == doctest::Approx(100 - 40 - 5));
}

TEST_CASE("World reserve pre-sizes entity and component storage") {
    World w;
    CHECK(w.capacity<Marker>() < 64);
    w.reserve<Transform, Marker>(64);
    CHECK(w.capacity<Transform>() >= 64);
    CHECK(w.capacity<Marker>() >= 64);
    CHECK(w.entityCapacity() >= 64);
}

TEST_CASE("World supports empty tag components") {
    struct Tag {};
    World w;
    auto e = w.spawn();
    w.add<Tag>(e);
    CHECK(w.has<Tag>(e));
    int n = 0;
    for (auto [entity] : w.view<Tag>()) n += entity == e ? 1 : 0;
    CHECK(n == 1);
    w.remove<Tag>(e);
    CHECK(!w.has<Tag>(e));
}

#if defined(NOVA_TRACK_ALLOCS)
TEST_CASE("World after reserve spawns, adds and destroys without allocating") {
    World w;
    w.reserve<Transform, Marker>(128);
    memory::markGameThread();
    for (int round = 0; round < 3; ++round) {
        auto before = memory::allocationCount();
        Entity es[10];
        for (auto& e : es) {
            e = w.spawn();
            w.add<Transform>(e);
            w.add<Marker>(e, Marker{1});
        }
        for (auto e : es) w.destroy(e);
        w.flushDestroyed();
        CHECK(memory::allocationCount() == before);
    }
}
#endif
