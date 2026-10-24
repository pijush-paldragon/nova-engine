#pragma once

#include <nova/ui/types.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace nova::ui {

struct Background {
    enum class Kind : u8 { None, Shape, Sprite };
    Kind kind = Kind::None;
    u8 radius = 0;        // Shape: corner radius in design units (snapped to 0, 8, 16, 24, 32)
    Color color = Color::White;
    std::string sprite;   // Sprite: "<atlas>/<region>" or a .png path
    Insets slice;         // Sprite: nine-slice insets in texels
};

// Overrides a state applies on top of the style.
struct StateStyle {
    std::optional<Background> background;
    std::optional<Color> tint;
    std::optional<Color> textColor;
    std::optional<f32> opacity;
    std::optional<f32> scale;
};

enum class VisualState : u8 { Normal, Pressed, Disabled, Checked };

struct Style {
    std::string extends;
    Background background;
    std::optional<Color> outlineColor;
    f32 outlineWidth = 2;
    Color tint = Color::White;
    f32 opacity = 1;
    f32 scale = 1;
    Insets padding;
    std::string font;
    f32 fontSize = 32;
    Color textColor = Color::White;
    std::string clickSound;
    StateStyle pressed, disabled, checked;
};

struct IdHashSv {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};

// Named styles. resolve() follows `extends` (a style without one extends "default") and applies
// a state's overrides; results are cached
// (references stay valid until the next set()).
class Theme {
public:
    void set(std::string name, Style style);
    bool has(std::string_view name) const { return entries_.find(name) != entries_.end(); }
    const Style& resolve(std::string_view name, VisualState state) const;
    // Bumped by every set(), so elements know to re-resolve fonts and sprites.
    u32 generation() const { return generation_; }

    // default, button, primary, secondary, danger, panel, hud, toggle, slider, progress.
    static Theme builtin();

private:
    struct Entry {
        Style raw;
        mutable Style resolved[4];
        mutable bool done[4] = {};
    };
    const Style& resolveRaw(std::string_view name, int depth) const;

    std::unordered_map<std::string, Entry, IdHashSv, std::equal_to<>> entries_;
    mutable Style merged_; // scratch for extends chains
    u32 generation_ = 1;
};

// Dev builds log each distinct message once per process; release builds log nothing.
void logOnce(const std::string& message);

// The style an element uses when its spec names none.
const char* defaultStyleName(Kind kind);

} // namespace nova::ui
