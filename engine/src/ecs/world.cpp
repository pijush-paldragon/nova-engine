#include <nova/ecs/world.hpp>

namespace nova {

World::~World() {
    // Clearing first makes every component's on_destroy hook fire while its owner is alive.
    registry_.clear();
    for (auto& hook : hooks_) {
        if (hook) hook->disconnect(registry_, *hook);
    }
}

void World::destroy(Entity e) {
    if (!alive(e) || registry_.all_of<PendingDestroy>(e)) return;
    registry_.emplace<PendingDestroy>(e);
    pending_.push_back(e);
}

void World::flushDestroyed() {
    // Hooks may destroy further entities; loop until nothing is pending.
    while (!pending_.empty()) {
        std::vector<Entity> batch;
        batch.swap(pending_);
        for (Entity e : batch) {
            if (registry_.valid(e)) registry_.destroy(e);
        }
    }
}

size_t World::count() const {
    // Alive entities: storage size minus the free list.
    return registry_.view<Entity>().size();
}

void World::removeHook(HookId id) {
    if (id >= hooks_.size() || !hooks_[id]) return;
    hooks_[id]->disconnect(registry_, *hooks_[id]);
    hooks_[id].reset();
}

void World::snapshotTransforms() {
    for (auto [e, t] : registry_.view<Transform>().each()) {
        t.prevPos = t.pos;
        t.prevRotation = t.rotation;
    }
}

void World::addSystem(Phase phase, SystemFn fn) {
    (phase == Phase::Fixed ? fixedSystems_ : updateSystems_).push_back(fn);
}

void World::runSystems(Phase phase, SystemContext& ctx) {
    for (SystemFn fn : (phase == Phase::Fixed ? fixedSystems_ : updateSystems_)) fn(ctx);
}

} // namespace nova
