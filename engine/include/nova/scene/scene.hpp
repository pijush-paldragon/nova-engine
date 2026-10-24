#pragma once

#include <nova/assets/asset_cache.hpp>
#include <nova/ecs/physics.hpp>
#include <nova/ecs/world.hpp>
#include <nova/render/camera.hpp>
#include <nova/scene/timers.hpp>
#include <nova/ui/canvas.hpp>

namespace nova {

class Assets;
class Audio;
class Text;
class Input;
class Renderer2D;
class SaveStore;
class SceneStack;
class Random;
struct AppOptions;
struct Services;

// Base class for game scenes. A scene owns its world (entities + physics), timers, camera and UI;
// everything else is reached through the accessors. Override only the hooks you need.
class Scene {
public:
    virtual ~Scene() = default;

    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onPause() {}  // another scene was pushed on top
    virtual void onResume() {} // the scene above was popped
    virtual void onBackground() {} // the app is going to the background (top scene only)
    virtual void onFixedUpdate(f32 dt) { (void)dt; }
    virtual void onUpdate(f32 dt) { (void)dt; }

    // When false, the scene below keeps rendering (pause overlays, dialogs).
    bool opaque = true;

    ScopeId scope() const { return scope_; }

protected:
    World& world() { return world_; }
    Physics& physics() { return physics_; }
    Timers& timers() { return timers_; }
    Camera2D& camera() { return camera_; }
    ui::Canvas& ui() { return ui_; }
    Assets& assets();
    Audio& audio();
    Text& text();
    Input& input();
    Renderer2D& renderer();
    SaveStore& save();
    SceneStack& scenes();
    const AppOptions& options();
    Random& random();

private:
    friend class SceneStack;
    friend class App;

    World world_;
    Physics physics_{world_, {0, 980}};
    Timers timers_;
    Camera2D camera_;
    ui::Canvas ui_;
    Services* services_ = nullptr;
    SceneStack* stack_ = nullptr;
    ScopeId scope_ = kGlobalScope;
};

} // namespace nova
