#include "scene/services.hpp"

#include <audio/audio_backend.hpp>
#include <input/input_internal.hpp>
#include <nova/assets/assets.hpp>
#include <nova/core/assert.hpp>
#include <nova/core/log.hpp>
#include <nova/scene/scene_stack.hpp>
#include <render/renderer_backend.hpp>

namespace nova {

// ---- Scene accessors --------------------------------------------------------------------

Assets& Scene::assets() { NOVA_ASSERT(services_->assets, "no Assets"); return *services_->assets; }
Audio& Scene::audio() { NOVA_ASSERT(services_->audio, "no Audio"); return *services_->audio; }
Input& Scene::input() { NOVA_ASSERT(services_->input, "no Input"); return *services_->input; }
Renderer2D& Scene::renderer() { NOVA_ASSERT(services_->renderer, "no renderer"); return *services_->renderer; }
SaveStore& Scene::save() { NOVA_ASSERT(services_->save, "no SaveStore"); return *services_->save; }
SceneStack& Scene::scenes() { return *stack_; }
const AppOptions& Scene::options() { NOVA_ASSERT(services_->options, "no options"); return *services_->options; }
Random& Scene::random() { NOVA_ASSERT(services_->random, "no Random"); return *services_->random; }

// ---- SceneStack ------------------------------------------------------------------------

void SceneStack::enter(std::unique_ptr<Scene> scene) {
    scene->services_ = &services_;
    scene->stack_ = this;
    scene->scope_ = nextScope_++;
    services_.assets->setActiveScope(scene->scope_);
    Scene& entered = *scene;
    scenes_.push_back(std::move(scene));
    entered.onEnter();
}

void SceneStack::exitTop() {
    Scene& top = *scenes_.back();
    services_.assets->setActiveScope(top.scope_);
    top.onExit();
    top.timers_.clear();
    ScopeId scope = top.scope_;
    scenes_.pop_back(); // destroys the scene: its world, bodies and timers go with it
    services_.assets->releaseScope(scope);
}

void SceneStack::applyPending() {
    if (ops_.empty()) return;
    // Ops queued from inside onEnter/onExit are appended and handled in this same pass.
    for (size_t i = 0; i < ops_.size(); ++i) {
        Op op = ops_[i].op;
        std::unique_ptr<Scene> scene = std::move(ops_[i].scene);
        switch (op) {
            case Op::Push:
                if (!scenes_.empty()) {
                    services_.assets->setActiveScope(scenes_.back()->scope_);
                    scenes_.back()->onPause();
                }
                enter(std::move(scene));
                break;
            case Op::Replace:
                if (!scenes_.empty()) exitTop();
                enter(std::move(scene));
                break;
            case Op::Pop:
                if (scenes_.size() <= 1) {
                    log::error("SceneStack: pop() refused, it would leave no scene");
                    break;
                }
                exitTop();
                services_.assets->setActiveScope(scenes_.back()->scope_);
                scenes_.back()->onResume();
                break;
        }
    }
    ops_.clear();
    services_.assets->purgeUnreferenced();
    if (!scenes_.empty()) services_.assets->setActiveScope(scenes_.back()->scope_);
}

void SceneStack::clear() {
    ops_.clear();
    while (!scenes_.empty()) exitTop();
    if (services_.assets) services_.assets->purgeUnreferenced();
}

} // namespace nova
