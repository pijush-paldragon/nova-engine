#include "app/app.hpp"

#include "ui/ui_backend.hpp"

#include "platform/asset_io.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/core/assert.hpp>
#include <nova/core/log.hpp>
#include <nova/ecs/particles.hpp>
#include <nova/ecs/render_bridge.hpp>
#include <nova/ecs/tween.hpp>

#include <SDL3/SDL.h>

#include <algorithm>

namespace nova {
namespace {

constexpr size_t kFrameArenaBytes = 1 << 20;
constexpr f64 kMaxFrameSeconds = 0.25;
constexpr i64 kAllocWarmupFrames = 120;      // after startup
constexpr i64 kTransitionWarmupFrames = 60;  // after a scene change (loading may allocate)

} // namespace

App::App() : random_(SDL_GetTicksNS() | 1), frame_(kFrameArenaBytes) {}

App::~App() { shutdown(); }

Result<void> App::init(int argc, char** argv) {
    memory::markGameThread();
    options_ = parseAppOptions(argc, argv);
    config_ = configureGame();
    if (!config_.firstScene) return Result<void>::fail("GameConfig.firstScene is not set");

    // Before any window: a damaged or missing pack ends the process here (reportIntegrityFailure).
    MountResult pack = mountPack();
    if (pack == MountResult::Corrupt) return Result<void>::fail("asset pack damaged");
    if (pack == MountResult::NotPresent && detail::packRequired()) {
        reportIntegrityFailure("pack missing");
        return Result<void>::fail("asset pack missing");
    }

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

    audio_ = AudioBackend::create();
    auto text = TextBackend::create(renderer_->sdl());
    if (!text) return Result<void>::fail(text.error().message);
    text_ = std::move(text.value());
    text_->setPixelScale(renderer_->pixelScale());

    RendererBackend* r = renderer_.get();
    assets_ = std::make_unique<Assets>(
        [r](std::string_view path) -> std::optional<TextureHandle> {
            TextureHandle h = r->loadTexture(path);
            return h.valid() ? std::optional<TextureHandle>(h) : std::nullopt;
        },
        [r](TextureHandle h) { r->destroyTexture(h); }, r->checkerboard());
    AudioBackend* a = audio_.get();
    assets_->setAudioLoaders([a](std::string_view p) { return a->loadSound(p); },
                             [a](SoundHandle h) { a->unloadSound(h); },
                             [a](std::string_view p) { return a->loadMusic(p); },
                             [a](MusicHandle h) { a->unloadMusic(h); });
    TextBackend* t = text_.get();
    assets_->setFontLoader([t](std::string_view key) { return t->loadFont(key); },
                           [t](FontHandle h) { t->unloadFont(h); });

    char* pref = SDL_GetPrefPath(config_.org, config_.app);
    save_ = std::make_unique<SaveStore>(pref ? std::string(pref) : std::string());
    SDL_free(pref);
    save_->load();

    services_.renderer = renderer_.get();
    services_.input = &input_;
    services_.assets = assets_.get();
    services_.audio = audio_.get();
    services_.text = text_.get();
    services_.save = save_.get();
    services_.options = &options_;
    services_.random = &random_;
    services_.frame = &frame_;
    uiShared_ = ui::createUiShared(*renderer_);
    services_.ui = uiShared_.get();

    scenes_ = std::make_unique<SceneStack>(services_);
    scenes_->pushScene(config_.firstScene());
    scenes_->applyPending();

#if defined(NOVA_DEV_TOOLS)
    overlay_ = SDL_getenv("NOVA_DEBUG_OVERLAY") != nullptr; // also toggled by F1 / three-finger tap
#endif
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

#if defined(NOVA_DEV_TOOLS)
    if (options_.failOnAlloc && SDL_getenv("NOVA_ALLOC_TRAP")) {
        memory::setAllocationTrap(frameCount_ >= kAllocWarmupFrames && framesSinceTransition_ >= kTransitionWarmupFrames);
    }
#endif
    if (options_.maxFrames > 0 && frameCount_ + 1 == options_.maxFrames && !options_.screenshotPath.empty()) {
        renderer_->requestScreenshot(options_.screenshotPath);
    }
    u64 allocsBefore = memory::allocationCount();
    runFrame(dt);
    allocsLastFrame_ = memory::allocationCount() - allocsBefore;
    ++frameCount_;

    const bool steadyState = frameCount_ > kAllocWarmupFrames && framesSinceTransition_ > kTransitionWarmupFrames;
    if (options_.failOnAlloc && steadyState && allocsLastFrame_ > 0) {
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
    SystemContext ctx{top->world_, input_, top->physics_, *assets_, *audio_, top->camera_, frame_, random_, step};
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
    updateTweens(top->world_, f32(dt));
    updateParticles(top->world_, f32(dt), random_);
    top->timers_.update(f32(dt));
    top->camera_.update(f32(dt), random_);
    ui::uiUpdate(top->ui_, f32(dt));

#if defined(NOVA_DEV_TOOLS)
    if (input_.keyPressed(Key::F1) || input_.threeFingerTap()) overlay_ = !overlay_;
#endif
    render(*top);

    top->world_.flushDestroyed();
    ui::uiFlush(top->ui_);
    if (scenes_->applyPending()) framesSinceTransition_ = 0;
    else ++framesSinceTransition_;
    input_.beginFrame(f32(dt));
}

void App::render(Scene& top) {
    text_->setPixelScale(renderer_->pixelScale());
    renderer_->beginFrame();
    scenes_->forEachVisible([&](Scene& scene) {
        batch_.begin();
        f32 alpha = &scene == &top ? clock_.alpha() : 1.f;
        Vec2 origin = scene.camera_.position - scene.camera_.shakeOffset();
        collectSprites(scene.world_, batch_, alpha, origin);
        collectParticles(scene.world_, batch_, origin);
        renderer_->submit(batch_);
        text_->collectAndDraw(scene.world_, renderer_->fit(), origin); // text above the scene's sprites
        ui::uiDraw(scene.ui_, *renderer_, uiBatch_);                  // the scene's UI above its world
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
    if (audio_) audio_->pauseAll();
    if (save_) {
        if (auto r = save_->flush(); !r) log::error("%s", r.error().message.c_str());
    }
    log::info("Entered background: simulation paused, save flushed");
}

void App::enterForeground() {
    if (!backgrounded_) return;
    backgrounded_ = false;
    clock_.reset(); // don't fast-forward through the time spent in the background
    if (audio_) audio_->resumeAll();
    lastTicks_ = SDL_GetTicksNS();
    log::info("Entered foreground");
}

bool App::teardownAndCheckLeaks() {
    if (save_) (void)save_->flush();
    if (scenes_) scenes_->clear();
    services_.assets = nullptr;
    assets_.reset();
    if (u32 failures = detail::assertFailureCount(); failures > 0) {
        log::error("Run had %u failed assertion(s)", failures);
        return false;
    }
    u32 textures = renderer_ ? renderer_->liveTextureCount() : 0;
    u32 sounds = audio_ ? audio_->liveAudioCount() : 0;
    u32 fonts = text_ ? text_->liveFontCount() : 0;
    u32 labels = text_ ? text_->liveTextCount() : 0;
    // The checkerboard and the UI shape texture live with the renderer.
    const u32 engineTextures = 1 + (uiShared_ && uiShared_->shapes.valid() ? 1 : 0);
    bool clean = textures <= engineTextures && sounds == 0 && fonts == 0 && labels == 0;
    if (clean) log::info("Leak check: clean");
    else log::error("Leak check: still alive after teardown: %u textures, %u sounds, %u fonts, %u labels",
                    textures > engineTextures ? textures - engineTextures : 0, sounds, fonts, labels);
    return clean;
}

void App::shutdown() {
    if (shutDown_) return;
    shutDown_ = true;
    if (save_) (void)save_->flush();
    scenes_.reset(); // scenes (and their worlds) before the assets they reference
    services_.assets = nullptr;
    assets_.reset();
    services_.ui = nullptr;
    uiShared_.reset();
    text_.reset();
    audio_.reset();
    unmountPack(); // after everything that streams from it (music)
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
