#include "app/app.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/core/log.hpp>
#include <nova/ecs/render_bridge.hpp>

#include <SDL3/SDL.h>

#include <algorithm>

namespace nova {
namespace {

constexpr size_t kFrameArenaBytes = 1 << 20;
constexpr f64 kMaxFrameSeconds = 0.25;
constexpr i64 kAllocWarmupFrames = 120;

} // namespace

App::App() : random_(SDL_GetTicksNS() | 1), frame_(kFrameArenaBytes) {}

App::~App() { shutdown(); }

Result<void> App::init(int argc, char** argv) {
    memory::markGameThread();
    options_ = parseAppOptions(argc, argv);
    config_ = configureGame();
    if (!config_.firstScene) return Result<void>::fail("GameConfig.firstScene is not set");

    SDL_SetAppMetadata(config_.title, "1.0", nullptr);
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "ambient");
    bool portrait = config_.orientation == Orientation::Portrait;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, portrait ? "Portrait PortraitUpsideDown" : "LandscapeLeft LandscapeRight");

    if (!SDL_Init(SDL_INIT_VIDEO)) return Result<void>::fail(std::string("SDL_Init failed: ") + SDL_GetError());
    sdlInitialized_ = true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) log::warn("Audio unavailable: %s", SDL_GetError());

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_ANDROID)
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    // Desktop: a window with the design aspect ratio, 960 points on the long side.
    f32 aspect = config_.designSize.x / config_.designSize.y;
    int w = portrait ? int(960 * aspect) : 960;
    int h = portrait ? 960 : int(960 / aspect);
    window_.reset(SDL_CreateWindow(config_.title, w, h, flags));
    if (!window_) return Result<void>::fail(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    auto renderer = RendererBackend::create(window_.get(), config_.designSize, config_.clearColor);
    if (!renderer) return Result<void>::fail(renderer.error().message);
    renderer_ = std::move(renderer.value());

    RendererBackend* r = renderer_.get();
    assets_ = std::make_unique<Assets>(
        [r](std::string_view path) -> std::optional<TextureHandle> {
            TextureHandle h = r->loadTexture(path);
            return h.valid() ? std::optional<TextureHandle>(h) : std::nullopt;
        },
        [r](TextureHandle h) { r->destroyTexture(h); }, r->checkerboard());

    char* pref = SDL_GetPrefPath(config_.org, config_.app);
    save_ = std::make_unique<SaveStore>(pref ? std::string(pref) : std::string());
    SDL_free(pref);
    save_->load();

    services_.renderer = renderer_.get();
    services_.input = &input_;
    services_.assets = assets_.get();
    services_.save = save_.get();
    services_.options = &options_;
    services_.random = &random_;
    services_.frame = &frame_;

    scenes_ = std::make_unique<SceneStack>(services_);
    scenes_->pushScene(config_.firstScene());
    scenes_->applyPending();

    lastTicks_ = SDL_GetTicksNS();
    log::info("%s started (%s, %.0fx%.0f design)", config_.title, renderer_->backendName(),
              double(config_.designSize.x), double(config_.designSize.y));
    return Result<void>::success();
}

SDL_AppResult App::iterate() {
    if (shutDown_) return SDL_APP_SUCCESS;
    if (backgrounded_) return SDL_APP_CONTINUE;

    u64 now = SDL_GetTicksNS();
    f64 dt = std::min(f64(now - lastTicks_) / 1e9, kMaxFrameSeconds);
    lastTicks_ = now;
    // Frame-limited runs (smoke tests) use a fixed dt so N frames always mean N/60 seconds of play.
    if (options_.maxFrames > 0) dt = clock_.step();

    if (options_.failOnAlloc && frameCount_ == kAllocWarmupFrames && SDL_getenv("NOVA_ALLOC_TRAP")) {
        memory::setAllocationTrap(true);
    }
    if (options_.maxFrames > 0 && frameCount_ + 1 == options_.maxFrames && !options_.screenshotPath.empty()) {
        renderer_->requestScreenshot(options_.screenshotPath);
    }
    u64 allocsBefore = memory::allocationCount();
    runFrame(dt);
    allocsLastFrame_ = memory::allocationCount() - allocsBefore;
    ++frameCount_;

    if (options_.failOnAlloc && frameCount_ > kAllocWarmupFrames && allocsLastFrame_ > 0) {
        log::error("Frame %lld made %llu heap allocations during gameplay", static_cast<long long>(frameCount_),
                   static_cast<unsigned long long>(allocsLastFrame_));
        teardownAndCheckLeaks();
        return SDL_APP_FAILURE;
    }
    if (scenes_->empty()) return SDL_APP_SUCCESS;
    if (options_.maxFrames > 0 && frameCount_ >= options_.maxFrames) {
        return teardownAndCheckLeaks() ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

void App::runFrame(f64 dt) {
    frame_.reset();
    fps_.add(f32(dt));
    Scene* top = scenes_->top();
    if (!top) return;

    const f32 step = f32(clock_.step());
    const i32 steps = clock_.advance(dt);
    top->physics_.beginFrame();
    SystemContext ctx{top->world_, input_, top->physics_, *assets_, top->camera_, frame_, random_, step};
    for (i32 i = 0; i < steps; ++i) {
        top->world_.snapshotTransforms();
        top->physics_.step(step);
        top->physics_.syncTransforms();
        top->world_.runSystems(Phase::Fixed, ctx);
        top->onFixedUpdate(step);
    }

    ctx.dt = f32(dt);
    top->onUpdate(f32(dt));
    top->world_.runSystems(Phase::Update, ctx);
    top->timers_.update(f32(dt));
    top->camera_.update(f32(dt), random_);

    if (input_.keyPressed(Key::F1) || input_.threeFingerTap()) overlay_ = !overlay_;
    render(*top);

    top->world_.flushDestroyed();
    scenes_->applyPending();
    input_.beginFrame(f32(dt));
}

void App::render(Scene& top) {
    renderer_->beginFrame();
    scenes_->forEachVisible([&](Scene& scene) {
        batch_.begin();
        f32 alpha = &scene == &top ? clock_.alpha() : 1.f;
        Vec2 origin = scene.camera_.position - scene.camera_.shakeOffset();
        collectSprites(scene.world_, batch_, alpha, origin);
        renderer_->submit(batch_);
    });
    if (overlay_) {
        FrameStats stats{fps_.fps(),
                         fps_.avgMs(),
                         renderer_->backendName(),
                         renderer_->drawCallsLastFrame(),
                         u32(top.world_.count()),
                         top.physics_.physicsWorld().bodyCount(),
                         allocsLastFrame_};
        drawDebugOverlay(renderer_->sdl(), stats, renderer_->pixelScale());
    }
    renderer_->endFrame();
}

SDL_AppResult App::event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_TERMINATING:
            if (save_) (void)save_->flush();
            return SDL_APP_SUCCESS;
        case SDL_EVENT_LOW_MEMORY:
            log::warn("Low memory: releasing unused assets");
            if (assets_) assets_->purgeUnreferenced();
            break;
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            enterBackground();
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            enterForeground();
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            if (renderer_) renderer_->onResize();
            break;
        default:
            break;
    }
    if (renderer_) input_.handleEvent(e, renderer_->fit());
    return SDL_APP_CONTINUE;
}

void App::enterBackground() {
    if (backgrounded_) return;
    backgrounded_ = true;
    if (Scene* top = scenes_ ? scenes_->top() : nullptr) top->onBackground();
    if (save_) {
        if (auto r = save_->flush(); !r) log::error("%s", r.error().message.c_str());
    }
    log::info("Entered background: simulation paused, save flushed");
}

void App::enterForeground() {
    if (!backgrounded_) return;
    backgrounded_ = false;
    clock_.reset(); // don't fast-forward through the time spent in the background
    lastTicks_ = SDL_GetTicksNS();
    log::info("Entered foreground");
}

bool App::teardownAndCheckLeaks() {
    if (save_) (void)save_->flush();
    if (scenes_) scenes_->clear();
    services_.assets = nullptr;
    assets_.reset();
    u32 textures = renderer_ ? renderer_->liveTextureCount() : 0;
    bool clean = textures <= 1; // the checkerboard lives as long as the renderer
    if (clean) log::info("Leak check: clean");
    else log::error("Leak check: %u textures still alive after teardown", textures - 1);
    return clean;
}

void App::shutdown() {
    if (shutDown_) return;
    shutDown_ = true;
    if (save_) (void)save_->flush();
    scenes_.reset(); // scenes (and their worlds) before the assets they reference
    services_.assets = nullptr;
    assets_.reset();
    renderer_.reset();
    window_.reset();
    if (sdlInitialized_) SDL_Quit();
}

void App::fatal(const std::string& message) {
    log::error("Fatal: %s", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, config_.title, message.c_str(), window_.get());
}

// ---- SDL main-callback bridge (called from entry.cpp, compiled into the game) ------------

namespace detail {
namespace {
std::unique_ptr<App>& theApp() {
    static std::unique_ptr<App> app;
    return app;
}
} // namespace

SDL_AppResult appInit(int argc, char** argv) {
    theApp() = std::make_unique<App>();
    if (auto r = theApp()->init(argc, argv); !r) {
        theApp()->fatal(r.error().message);
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult appIterate() { return theApp() ? theApp()->iterate() : SDL_APP_FAILURE; }

SDL_AppResult appEvent(const SDL_Event& e) { return theApp() ? theApp()->event(e) : SDL_APP_FAILURE; }

void appQuit() { theApp().reset(); }

} // namespace detail
} // namespace nova
