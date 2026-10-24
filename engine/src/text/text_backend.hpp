#pragma once

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

private:
    friend class TextRun;
    TextBackend() = default;
    SDL_Renderer* renderer_ = nullptr;
};

// One piece of laid-out text owned by engine code (UI labels). Re-lays out only when its font,
// string or the pixel scale changes; changing colour or opacity is cheap.
class TextRun {
public:
    explicit TextRun(TextBackend& backend) : backend_(&backend) {}
    ~TextRun();
    TextRun(const TextRun&) = delete;
    TextRun& operator=(const TextRun&) = delete;

    // False if the font is gone or the text could not be created.
    bool set(FontHandle font, std::string_view s);
    // Size in design units ({0, 0} for empty text).
    Vec2 size();
    bool empty() const { return string_.empty(); }
    // Draws with its top-left at `topLeft` (design units), scaled about that point.
    void draw(Vec2 topLeft, const ScreenFit& fit, Color color, f32 scale);

private:
    bool refresh(); // re-lays out after a font size (pixel scale) change

    TextBackend* backend_;
    TTF_Text* text_ = nullptr;
    FontHandle font_;
    std::string string_;
    u32 fontGeneration_ = 0;
    Color color_{0, 0, 0, 0};
};

} // namespace nova
