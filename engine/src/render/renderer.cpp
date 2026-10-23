#include "render/renderer_backend.hpp"

#include "platform/asset_io.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/core/log.hpp>
#include <nova/platform/file.hpp>

#include <SDL3/SDL.h>

#include <cstddef>

namespace nova {

static_assert(sizeof(Vertex) == sizeof(SDL_Vertex));
static_assert(offsetof(Vertex, pos) == offsetof(SDL_Vertex, position));
static_assert(offsetof(Vertex, color) == offsetof(SDL_Vertex, color));
static_assert(offsetof(Vertex, uv) == offsetof(SDL_Vertex, tex_coord));

namespace {

const char* defaultDriverList() {
#if defined(SDL_PLATFORM_IOS)
    return "metal,opengles2";
#elif defined(SDL_PLATFORM_ANDROID)
    return "vulkan,opengles2";
#elif defined(SDL_PLATFORM_MACOS)
    return "metal,opengl";
#else
    return "vulkan,opengl,opengles2";
#endif
}

} // namespace

Vec2 Renderer2D::textureSize(TextureHandle texture) const {
    // Every Renderer2D is a RendererBackend (the base constructor is protected).
    return static_cast<const RendererBackend*>(this)->textureSizeOf(texture);
}

Result<std::unique_ptr<RendererBackend>> RendererBackend::create(SDL_Window* window, Vec2 design, Color clear) {
#if defined(NOVA_DEV_TOOLS)
    const char* requested = SDL_getenv("NOVA_RENDER_DRIVER");
#else
    const char* requested = nullptr;
#endif
    const char* drivers = (requested && *requested) ? requested : defaultDriverList();

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, drivers);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        log::warn("No renderer from '%s' (%s); letting SDL choose", drivers, SDL_GetError());
        SDL_ResetHint(SDL_HINT_RENDER_DRIVER);
        renderer = SDL_CreateRenderer(window, nullptr);
    }
    if (!renderer) {
        return Result<std::unique_ptr<RendererBackend>>::fail(std::string("SDL_CreateRenderer failed: ") +
                                                              SDL_GetError());
    }
    if (!SDL_SetRenderVSync(renderer, 1)) log::warn("VSync unavailable: %s", SDL_GetError());

    std::unique_ptr<RendererBackend> backend(new RendererBackend(window, renderer, design, clear));
    log::info("Renderer backend: %s", backend->backendName());
    return backend;
}

RendererBackend::RendererBackend(SDL_Window* window, SDL_Renderer* renderer, Vec2 design, Color clear)
    : window_(window), renderer_(renderer), design_(design) {
    backendName_ = SDL_GetRendererName(renderer);
    clearColor_ = clear;

    // 2x2 magenta/black checkerboard stands in for any missing texture.
    const u32 pixels[4] = {0xFFFF00FFu, 0xFF000000u, 0xFF000000u, 0xFFFF00FFu};
    SDL_Texture* checker = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 2, 2);
    if (checker) {
        SDL_UpdateTexture(checker, nullptr, pixels, 2 * sizeof(u32));
        SDL_SetTextureScaleMode(checker, SDL_SCALEMODE_NEAREST);
        checkerboard_ = addTexture(checker, {2, 2});
    }
    onResize();
}

RendererBackend::~RendererBackend() { textures_.clear(); }

void RendererBackend::onResize() {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer_.get(), &w, &h);
    fit_ = computeFit({f32(w), f32(h)}, design_);

    SDL_Rect safe{0, 0, 0, 0};
    f32 density = SDL_GetWindowPixelDensity(window_);
    if (density <= 0) density = 1;
    fit_.pixelDensity = density;
    if (SDL_GetWindowSafeArea(window_, &safe) && safe.w > 0 && safe.h > 0) {
        safeArea_ = {fit_.visible.x + f32(safe.x) * density / fit_.scale,
                     fit_.visible.y + f32(safe.y) * density / fit_.scale,
                     f32(safe.w) * density / fit_.scale, f32(safe.h) * density / fit_.scale};
    } else {
        safeArea_ = fit_.visible;
    }
}

void RendererBackend::beginFrame() {
    drawCalls_ = 0;
    memory::DriverAllocScope driver;
    SDL_SetRenderDrawColor(renderer_.get(), clearColor_.r, clearColor_.g, clearColor_.b, clearColor_.a);
    SDL_RenderClear(renderer_.get());
}

void RendererBackend::submit(SpriteBatch& batch) {
    batch.build([this](TextureHandle t) { return textureSizeOf(t); });
    if (batch.runs().empty()) return;
    batch.toPixels(fit_.scale, {fit_.visible.x, fit_.visible.y});

    auto vertices = batch.vertices();
    auto indices = batch.indices();
    const auto* sdlVertices = reinterpret_cast<const SDL_Vertex*>(vertices.data());
    for (const BatchRun& run : batch.runs()) {
        SDL_Texture* texture = resolve(run.texture);
        memory::DriverAllocScope driver;
        SDL_SetTextureBlendMode(texture, run.blend == BlendMode::Additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(renderer_.get(), texture, sdlVertices, int(vertices.size()),
                           indices.data() + run.firstIndex, int(run.indexCount));
        ++drawCalls_;
    }
}

void RendererBackend::endFrame() {
    {
        memory::DriverAllocScope driver;
        if (!screenshotPath_.empty()) {
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer_.get(), nullptr)) {
                if (SDL_SavePNG(shot, screenshotPath_.c_str())) log::info("Screenshot saved to %s", screenshotPath_.c_str());
                else log::error("Screenshot failed: %s", SDL_GetError());
                SDL_DestroySurface(shot);
            }
            screenshotPath_.clear();
        }
        SDL_RenderPresent(renderer_.get());
    }
    drawCallsLastFrame_ = drawCalls_;
}

TextureHandle RendererBackend::loadTexture(std::string_view path) {
    std::string full = assetPath(path);
    SDL_IOStream* io = openAsset(path);
    if (!io) {
        log::error("Texture '%s' not found: %s", full.c_str(), SDL_GetError());
        return {};
    }
    SDL_Surface* surface = SDL_LoadPNG_IO(io, true);
    if (!surface) {
        log::error("Texture '%s' is not a readable PNG: %s", full.c_str(), SDL_GetError());
        return {};
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_.get(), surface);
    Vec2 size{f32(surface->w), f32(surface->h)};
    SDL_DestroySurface(surface);
    if (!texture) {
        log::error("Texture '%s' upload failed: %s", full.c_str(), SDL_GetError());
        return {};
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return addTexture(texture, size);
}

void RendererBackend::destroyTexture(TextureHandle texture) {
    if (texture == checkerboard_) return;
    textures_.remove(texture);
}

Vec2 RendererBackend::textureSizeOf(TextureHandle texture) const {
    if (const TextureEntry* e = textures_.get(texture)) return e->size;
    if (const TextureEntry* c = textures_.get(checkerboard_)) return c->size;
    return {1, 1};
}

TextureHandle RendererBackend::addTexture(SDL_Texture* texture, Vec2 size) {
    return textures_.insert(TextureEntry{std::unique_ptr<SDL_Texture, TextureDeleter>(texture), size});
}

SDL_Texture* RendererBackend::resolve(TextureHandle texture) const {
    if (const TextureEntry* e = textures_.get(texture)) return e->texture.get();
    const TextureEntry* c = textures_.get(checkerboard_);
    return c ? c->texture.get() : nullptr;
}

} // namespace nova
