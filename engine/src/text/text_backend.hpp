#pragma once

#include <nova/core/result.hpp>
#include <nova/render/camera.hpp>
#include <nova/text/text.hpp>

#include <optional>
#include <string_view>

struct SDL_Renderer;

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

    // Fonts are rasterised at design size x pixel scale so glyphs stay crisp on every screen.
    void setPixelScale(f32 scale);

    // Lays out changed labels and draws every visible one, sorted by layer.
    void collectAndDraw(World& world, const ScreenFit& fit, Vec2 viewOrigin);

    u32 liveTextCount() const;
    u32 liveFontCount() const;

private:
    TextBackend() = default;
};

} // namespace nova
