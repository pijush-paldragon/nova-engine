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

// A style property that remembers whether it was set, so a derived style can set a value equal to
// the default (fontSize 32, white text, no padding) and still override its parent. Assigning, or
// editing through ->, marks it set; reading through * or -> on a const style doesn't.
template <class T>
class Prop {
public:
    Prop() = default;
    Prop(T fallback) : value_(std::move(fallback)) {} // a default, not "set"
    Prop& operator=(T v) {
        value_ = std::move(v);
        set_ = true;
        return *this;
    }
    const T& operator*() const { return value_; }
    const T& value() const { return value_; }
    const T* operator->() const { return &value_; }
    T* operator->() {
        set_ = true;
        return &value_;
    }
    bool isSet() const { return set_; }

private:
    T value_{};
    bool set_ = false;
};

struct Style {
    std::string extends;
    Prop<Background> background;
    Prop<std::optional<Color>> outlineColor;
    Prop<f32> outlineWidth = 2.f;
    Prop<Color> tint = Color::White;
    Prop<f32> opacity = 1.f;
    Prop<f32> scale = 1.f;
    Prop<Insets> padding;
    Prop<std::string> font;
    Prop<std::string> boldFont; // rich text [b]; unset = synthetic bold of `font`
    Prop<f32> fontSize = 32.f;
    Prop<Color> textColor = Color::White;
    Prop<std::string> clickSound;
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
    // Changes only the properties `style` sets (keeping the existing style's others and its
    // extends, unless given); a new name is simply set.
    void patch(const std::string& name, const Style& style);
    bool has(std::string_view name) const { return entries_.find(name) != entries_.end(); }
    const Style& resolve(std::string_view name, VisualState state) const;
    // Bumped by every set(), so elements know to re-resolve fonts and sprites.
    u32 generation() const { return generation_; }

    // Named colours for rich text ([color=gold]), from theme.json's "colors".
    void setColor(std::string name, Color color);
    std::optional<Color> color(std::string_view name) const;

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
    std::unordered_map<std::string, Color, IdHashSv, std::equal_to<>> colors_;
    mutable Style merged_; // scratch for extends chains
    u32 generation_ = 1;
};

// Dev builds log each distinct message once per process; release builds log nothing.
void logOnce(const std::string& message);

// Logs a message once per (kind, name); the message is only built the first time, so a lookup
// repeated every frame (a typo'd style) costs a set lookup, never an allocation.
enum class LogKey : u8 { UnknownStyle, NoFont, BadSprite, StyleExtends, UnknownColor, Count };
bool firstReport(LogKey key, std::string_view name);
template <class F>
void logOnceFor(LogKey key, std::string_view name, F&& makeMessage) {
#if defined(NOVA_DEV_TOOLS)
    if (firstReport(key, name)) logOnce(makeMessage());
#else
    (void)key;
    (void)name;
    (void)makeMessage;
#endif
}

// The style an element uses when its spec names none.
const char* defaultStyleName(Kind kind);

} // namespace nova::ui
