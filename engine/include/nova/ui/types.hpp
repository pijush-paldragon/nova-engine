#pragma once

// UI value types. UiSpec mirrors the layout-file fields one to one, so a screen built in code
// and one loaded from JSON produce the same elements.

#include <nova/core/handle.hpp>
#include <nova/core/math.hpp>

#include <optional>
#include <string>

namespace nova::ui {

struct ElementTag;
using UiHandle = Handle<ElementTag>;

enum class Kind : u8 { Panel, Image, Label, Button, Toggle, Slider, ProgressBar };

enum class LayoutMode : u8 { Stack, Row, Column };
enum class Anchor : u8 { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
enum class Justify : u8 { Start, Center, End, SpaceBetween };
enum class Align : u8 { Start, Center, End, Stretch };
// Start and End follow the language's direction (Start is Right in a right-to-left language).
enum class TextAlignH : u8 { Left, Center, Right, Start, End };
// What a label does when its text doesn't fit its box: draw past it, or step the font down.
enum class Overflow : u8 { Clip, Shrink };
enum class ImageFit : u8 { Stretch, Contain, Cover };
enum class ToggleLook : u8 { Switch, Checkbox };
enum class FillDirection : u8 { Right, Left, Up, Down };
enum class Transition : u8 { None, Fade, Pop, SlideUp, SlideDown, SlideLeft, SlideRight };

// One axis of an element's size: fixed pixels, a weighted share of the leftover space, or the
// size of its content.
struct Size {
    enum class Mode : u8 { Fixed, Fill, Fit };
    Mode mode = Mode::Fit;
    f32 value = 0; // pixels (Fixed) or weight (Fill)

    static constexpr Size px(f32 pixels) { return {Mode::Fixed, pixels}; }
    static constexpr Size fill(f32 weight = 1) { return {Mode::Fill, weight}; }
    static constexpr Size fit() { return {Mode::Fit, 0}; }
    bool operator==(const Size&) const = default;
};

struct Insets {
    f32 top = 0, right = 0, bottom = 0, left = 0;
    static constexpr Insets all(f32 v) { return {v, v, v, v}; }
    bool operator==(const Insets&) const = default;
};

// Every property an element can have; each kind reads the fields that apply to it.
struct UiSpec {
    // Every field has a default initializer, so designated initializers may skip any of them.
    std::string id{};
    std::string style{};
    std::string text{};
    std::string sprite{};
    std::string spriteRtl{}; // image: drawn instead of `sprite` in right-to-left languages
    std::string icon{};

    Size width = Size::fit();
    Size height = Size::fit();
    f32 minWidth = 0, minHeight = 0;
    f32 maxWidth = 1e9f, maxHeight = 1e9f;
    Anchor anchor = Anchor::TopLeft;
    Vec2 offset{};

    LayoutMode layout = LayoutMode::Stack;
    f32 gap = 0;
    Insets padding{};
    Justify justify = Justify::Start;
    Align align = Align::Start;
    bool safeArea = false;

    bool visible = true;
    bool enabled = true;
    f32 opacity = 1;
    bool blocksInput = false;
    // Right-to-left languages mirror the layout (rows reverse; anchors, start/end, padding and
    // offset.x swap sides). false keeps this element and its subtree as authored.
    bool mirror = true;

    TextAlignH textAlign = TextAlignH::Start;
    bool wrap = false;       // labels and buttons: break lines at the content width
    i32 maxLines = 0;        // 0 = no limit; overflow ends with "…"
    Overflow overflow = Overflow::Clip;
    f32 minScale = 0.6f;     // Shrink: smallest font size, as a share of the style's
    f32 lineSpacing = 1;
    ImageFit imageFit = ImageFit::Stretch;
    Insets slice{};          // nine-slice insets in texels; all zero = stretch the whole sprite
    Color tint = Color::White;

    ToggleLook look = ToggleLook::Switch;
    bool on = false;
    f32 min = 0, max = 1, step = 0, value = 0;
    FillDirection direction = FillDirection::Right;
    f32 animate = 0; // progress bar: seconds to ease toward a new value
};

// How a popup opens and closes.
struct PopupOptions {
    f32 dim = 0.6f;               // backdrop opacity (black)
    bool closeOnBackdrop = false; // a tap outside the popup closes it
    // Unset: the layout file's root "enter"/"exit", else pop in and fade out.
    std::optional<Transition> enter;
    std::optional<Transition> exit;
    f32 duration = 0.2f;          // seconds, for enter and exit
};

} // namespace nova::ui
