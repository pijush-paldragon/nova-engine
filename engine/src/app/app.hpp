#pragma once

#include "app/debug_overlay.hpp"
#include "input/input_internal.hpp"
#include "render/renderer_backend.hpp"
#include "scene/services.hpp"

#include <nova/app/app_options.hpp>
#include <nova/app/fixed_step.hpp>
#include <nova/app/game_config.hpp>
#include <nova/assets/assets.hpp>
#include <nova/core/frame_arena.hpp>
#include <nova/core/random.hpp>
#include <nova/render/sprite_batch.hpp>
#include <nova/save/save_store.hpp>
#include <nova/scene/scene_stack.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <memory>
#include <string>

namespace nova {

// Owns every engine subsystem and runs the frame loop. Driven by the SDL main callbacks.
class App {
public:
    App();
    ~App();

    Result<void> init(int argc, char** argv);
    SDL_AppResult iterate();
    SDL_AppResult event(const SDL_Event& e);
    void shutdown();
    void fatal(const std::string& message);

private:
    struct WindowDeleter {
        void operator()(SDL_Window* w) const { SDL_DestroyWindow(w); }
    };

    void runFrame(f64 dt);
    void render(Scene& top);
    void enterBackground();
    void enterForeground();
    // Tears down scenes and assets, then reports anything still alive. Returns true if clean.
    bool teardownAndCheckLeaks();

    GameConfig config_;
    AppOptions options_;
    bool sdlInitialized_ = false;
    std::unique_ptr<SDL_Window, WindowDeleter> window_;
    std::unique_ptr<RendererBackend> renderer_;
    std::unique_ptr<Assets> assets_;
    std::unique_ptr<SaveStore> save_;
    InputBackend input_;
    Random random_;
    FrameArena frame_;
    Services services_;
    std::unique_ptr<SceneStack> scenes_;

    FixedStepClock clock_;
    SpriteBatch batch_;
    FpsCounter fps_;
    u64 lastTicks_ = 0;
    i64 frameCount_ = 0;
    u64 allocsLastFrame_ = 0;
    bool overlay_ = false;
    bool backgrounded_ = false;
    bool shutDown_ = false;
};

} // namespace nova
