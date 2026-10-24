#pragma once

#include "text/text_block.hpp"

#include <nova/core/result.hpp>
#include <nova/render/camera.hpp>
#include <nova/text/text.hpp>

#include <optional>
#include <string>
#include <string_view>

struct SDL_Renderer;
struct TTF_Text;
struct TTF_Font;

namespace nova {

class World;

// Engine-side text: owns the SDL_ttf renderer text engine and every font.
class TextBackend final : public Text {
public:
    static Result<std::unique_ptr<TextBackend>> create(SDL_Renderer* renderer);
    ~TextBackend();

    // key = "<path>@<sizePx>", e.g. "fonts/Fredoka.ttf@48". Size is in design units.
    std::optional<FontHandle> loadFont(std::string_view key);
    void unloadFont(FontHandle font);
    // The SDL_ttf font behind a handle (engine use and tests), or null.
    TTF_Font* ttfFont(FontHandle font) const;

    // Fonts are rasterised at design size x pixel scale so glyphs stay crisp on every screen.
    void setPixelScale(f32 scale);

    // Lays out changed labels and draws every visible one, sorted by layer.
    void collectAndDraw(World& world, const ScreenFit& fit, Vec2 viewOrigin);

    u32 liveTextCount() const;
    u32 liveFontCount() const;
    f32 pixelScale() const { return pixelScale_; }

    // For TextBlock: a counted SDL_ttf text object, and what invalidates layouts.
    TtfTextPtr createText(TTF_Font* font, std::string_view s);
    u32 fontGeneration() const;
    SDL_Renderer* renderer() const { return renderer_; }
    void countRebuild() { ++rebuilds_; }

private:
    TextBackend() = default;
    SDL_Renderer* renderer_ = nullptr;
};

} // namespace nova
