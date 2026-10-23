#pragma once

#include <nova/core/types.hpp>
#include <nova/ecs/components.hpp>
#include <nova/ecs/system.hpp>

#include <entt/entity/registry.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace nova {

enum class Entity : u32 {};
inline constexpr Entity kNullEntity = entt::null;

// The ECS registry for one scene. Wraps EnTT so game code never names entt:: directly.
// destroy() is deferred to the end of the frame: an entity stays valid (and visible to
// views) until flushDestroyed(), so systems never see one vanish mid-iteration.
class World {
public:
    using Registry = entt::basic_registry<Entity>;
    using HookFn = void (*)(void* user, World& world, Entity entity);
    using HookId = u32;

    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    ~World();

    Entity spawn() { return registry_.create(); }
    void destroy(Entity e);
    bool alive(Entity e) const { return e != kNullEntity && registry_.valid(e); }
    bool pendingDestroy(Entity e) const { return alive(e) && registry_.all_of<PendingDestroy>(e); }
    void flushDestroyed();
    size_t count() const;

    template <class C, class... Args>
    C& add(Entity e, Args&&... args) {
        C& c = registry_.emplace_or_replace<C>(e, std::forward<Args>(args)...);
        if constexpr (std::is_same_v<C, Transform>) {
            c.prevPos = c.pos;
            c.prevRotation = c.rotation;
        }
        return c;
    }
    template <class C>
    C& get(Entity e) { return registry_.get<C>(e); }
    template <class C>
    C* tryGet(Entity e) { return alive(e) ? registry_.try_get<C>(e) : nullptr; }
    template <class C>
    bool has(Entity e) const { return alive(e) && registry_.all_of<C>(e); }
    template <class C>
    void remove(Entity e) { if (alive(e)) registry_.remove<C>(e); }

    // Iterate as: for (auto [entity, a, b] : world.view<A, B>()) { ... }
    template <class... C>
    auto view() { return registry_.view<C...>().each(); }

    // Calls fn whenever a C is removed (component removal, entity destruction, World teardown).
    template <class C>
    HookId onDestroy(HookFn fn, void* user) {
        auto hook = std::make_unique<Hook>(Hook{fn, user, this, nullptr});
        Hook* raw = hook.get();
        registry_.on_destroy<C>().template connect<&Hook::invoke>(*raw);
        hook->disconnect = [](Registry& r, Hook& h) { r.on_destroy<C>().template disconnect<&Hook::invoke>(h); };
        hooks_.push_back(std::move(hook));
        return HookId(hooks_.size() - 1);
    }
    void removeHook(HookId id);

    // Engine: copy current transforms into prev* before each fixed step.
    void snapshotTransforms();

    Registry& registry() { return registry_; }

private:
    struct PendingDestroy {};
    struct Hook {
        HookFn fn;
        void* user;
        World* world;
        void (*disconnect)(Registry&, Hook&);
        void invoke(Registry&, Entity e) { fn(user, *world, e); }
    };

    Registry registry_;
    std::vector<Entity> pending_;
    std::vector<std::unique_ptr<Hook>> hooks_;
};

} // namespace nova
