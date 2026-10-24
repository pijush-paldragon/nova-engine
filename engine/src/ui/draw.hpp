#pragma once

#include "ui/style.hpp"

#include <nova/ui/canvas.hpp>

#include <nova/render/sprite_batch.hpp>

#include <array>
#include <string>
#include <memory>
#include <vector>

namespace nova {
class RendererBackend;
class TextBlock;
}

namespace nova::ui {

// A built-in shape in the shared shape texture, drawn nine-sliced.
struct ShapeRegion {
    Rect src;          // texels
    f32 insetTexels = 0;
    f32 insetDesign = 0;
};

inline constexpr std::array<u8, 5> kShapeRadii{0, 8, 16, 24, 32};

// Engine-wide UI state shared by every canvas: the theme and the generated shape texture.
struct UiShared {
    RendererBackend* renderer = nullptr;
    Theme theme = Theme::builtin();
    TextureHandle shapes;
    std::array<ShapeRegion, kShapeRadii.size()> filled{}, outline{};
    ShapeRegion circle;
    std::vector<std::string> themeFiles; // live reload of ui/theme.json (dev builds)
    std::vector<i64> themeStamps;

    UiShared() = default;
    UiShared(const UiShared&) = delete;
    UiShared& operator=(const UiShared&) = delete;
    ~UiShared();

    // The filled or outline shape for the nearest built-in radius.
    const ShapeRegion& shape(u8 radius, bool outlined) const;
};

std::unique_ptr<UiShared> createUiShared(RendererBackend& renderer);

struct TextItem {
    TextBlock* text = nullptr;
    Rect box;          // design units, before the layer transform's scale; lines align within box.w
    Vec2 topLeft;      // where it is drawn, transform applied
    f32 scale = 1;
    Color color;
    TextAlignH align = TextAlignH::Left;
};

struct DrawLayer {
    std::vector<SpriteCmd> sprites;
    std::vector<TextItem> texts;
};

struct DrawList {
    std::vector<DrawLayer> layers;
};

// Resolves styles, fonts, sprites and text where needed, then lays out dirty layers.
void refreshLayout(Canvas::Impl& impl);
// Pre-sizes the draw traversal's scratch space.
void reserveDrawScratch(size_t n);

} // namespace nova::ui
