#pragma once

#include <nova/core/handle.hpp>
#include <nova/core/result.hpp>
#include <nova/render/renderer.hpp>
#include <nova/render/sprite_batch.hpp>

#include <SDL3/SDL_render.h>

#include <memory>
#include <string_view>

namespace nova {

// Engine-side renderer: owns the SDL_Renderer and every texture.
class RendererBackend final : public Renderer2D {
public:
    // Tries the platform's preferred drivers (or $NOVA_RENDER_DRIVER), then anything SDL offers.
    static Result<std::unique_ptr<RendererBackend>> create(SDL_Window* window, Vec2 design, Color clear);
    ~RendererBackend();

    void onResize();
    void beginFrame();
    void submit(SpriteBatch& batch);
    void endFrame();

    // Returns an invalid handle (and logs) if the file is missing or not a PNG.
    TextureHandle loadTexture(std::string_view path);
    void destroyTexture(TextureHandle texture);
    TextureHandle checkerboard() const { return checkerboard_; }
    Vec2 textureSizeOf(TextureHandle texture) const;
    u32 liveTextureCount() const { return textures_.size(); }

    SDL_Renderer* sdl() const { return renderer_.get(); }
    SDL_Window* window() const { return window_; }
    Vec2 designSize() const { return design_; }

private:
    struct RendererDeleter {
        void operator()(SDL_Renderer* r) const { SDL_DestroyRenderer(r); }
    };
    struct TextureDeleter {
        void operator()(SDL_Texture* t) const { SDL_DestroyTexture(t); }
    };
    struct TextureEntry {
        std::unique_ptr<SDL_Texture, TextureDeleter> texture;
        Vec2 size;
    };

    RendererBackend(SDL_Window* window, SDL_Renderer* renderer, Vec2 design, Color clear);
    TextureHandle addTexture(SDL_Texture* texture, Vec2 size);
    SDL_Texture* resolve(TextureHandle texture) const;

    SDL_Window* window_;
    // Declared before textures_ so textures are destroyed first.
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer_;
    HandlePool<TextureEntry, TextureTag> textures_;
    TextureHandle checkerboard_;
    Vec2 design_;
    u32 drawCalls_ = 0;
};

} // namespace nova
