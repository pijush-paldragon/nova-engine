#pragma once

// Test support for the UI: log capture and headless services (software renderer, text, assets).

#include <input/input_internal.hpp>
#include <nova/assets/assets.hpp>
#include <nova/ui/ui.hpp>
#include <render/renderer_backend.hpp>
#include <scene/services.hpp>
#include <text/text_backend.hpp>
#include <ui/canvas_impl.hpp>
#include <ui/draw.hpp>
#include <ui/style.hpp>
#include <ui/ui_backend.hpp>

#include <SDL3/SDL.h>

#include <doctest/doctest.h>

#include <memory>

#include <string>
#include <vector>

namespace nova::testing {

// Collects every SDL log line while alive (nova::log routes through SDL).
class LogCapture {
public:
    LogCapture() {
        SDL_GetLogOutputFunction(&prevFn_, &prevData_);
        SDL_SetLogOutputFunction(&LogCapture::record, this);
    }
    ~LogCapture() { SDL_SetLogOutputFunction(prevFn_, prevData_); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    const std::vector<std::string>& lines() const { return lines_; }
    int count(const std::string& needle) const {
        int n = 0;
        for (const std::string& l : lines_) n += l.find(needle) != std::string::npos;
        return n;
    }
    void clear() { lines_.clear(); }

private:
    static void SDLCALL record(void* self, int, SDL_LogPriority, const char* message) {
        static_cast<LogCapture*>(self)->lines_.emplace_back(message);
    }
    SDL_LogOutputFunction prevFn_ = nullptr;
    void* prevData_ = nullptr;
    std::vector<std::string> lines_;
};

// A canvas with real engine services: software renderer (hidden window, 720x1280 design),
// text, assets reading NOVA_TEST_ASSET_ROOT, and the shared UI theme and textures.
struct UiFixture {
    SDL_Window* window = nullptr;
    std::unique_ptr<RendererBackend> renderer;
    std::unique_ptr<TextBackend> text;
    std::unique_ptr<Assets> assets;
    std::unique_ptr<ui::UiShared> shared;
    InputBackend input;
    Services services{};
    SpriteBatch batch;
    ui::Canvas canvas;

    UiFixture() {
        REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("ui", 360, 640, SDL_WINDOW_HIDDEN);
        REQUIRE(window);
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "NOVA_RENDER_DRIVER", "software", true);
        auto r = RendererBackend::create(window, {720, 1280}, Color::Black);
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "NOVA_RENDER_DRIVER");
        REQUIRE(r.ok());
        renderer = std::move(r.value());
        auto t = TextBackend::create(renderer->sdl());
        REQUIRE(t.ok());
        text = std::move(t.value());
        text->setPixelScale(renderer->pixelScale());
        RendererBackend* rp = renderer.get();
        assets = std::make_unique<Assets>(
            [rp](std::string_view path) -> std::optional<TextureHandle> {
                TextureHandle h = rp->loadTexture(path);
                return h.valid() ? std::optional<TextureHandle>(h) : std::nullopt;
            },
            [rp](TextureHandle h) { rp->destroyTexture(h); }, rp->checkerboard());
        TextBackend* tp = text.get();
        assets->setFontLoader([tp](std::string_view key) { return tp->loadFont(key); },
                              [tp](FontHandle h) { tp->unloadFont(h); });
        shared = ui::createUiShared(*renderer);
        services.renderer = renderer.get();
        services.text = text.get();
        services.assets = assets.get();
        services.input = &input;
        services.ui = shared.get();
        ui::uiAttach(canvas, &services, kGlobalScope);
    }
    ~UiFixture() {
        canvas.root(); // keep canvas alive until here; elements free before the services they use
        ui::uiReset(canvas);
        assets->releaseScope(kGlobalScope);
        shared.reset();
        assets.reset();
        text.reset();
        renderer.reset();
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    // One frame: update (layout) and draw.
    void frame(f32 dt = 1.f / 60.f) {
        ui::uiUpdate(canvas, dt);
        renderer->beginFrame();
        ui::uiDraw(canvas, *renderer, batch);
        renderer->endFrame();
        ui::uiFlush(canvas);
    }
};

} // namespace nova::testing
