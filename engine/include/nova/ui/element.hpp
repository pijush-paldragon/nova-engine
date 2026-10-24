#pragma once

// Typed references to UI elements. A reference is a canvas pointer plus a generational handle:
// cheap to copy, and safe after the element is gone (every call becomes a no-op).

#include <nova/ecs/tween.hpp>
#include <nova/ui/types.hpp>

#include <functional>
#include <string_view>

namespace nova::ui {

class Canvas;
class PanelRef;
class ImageRef;
class LabelRef;
class ButtonRef;
class ToggleRef;
class SliderRef;
class ProgressRef;

class ElementRef;

// Builds animations on one element: each of scale/fade/move starts a track (replacing a running
// track of the same kind); ease/delay/yoyo/loop/onDone apply to the track started last.
//   button.animate().scale(1.1f, 0.15f).ease(Ease::OutBack).yoyo();
class Animator {
public:
    Animator(Canvas* canvas, UiHandle h) : canvas_(canvas), h_(h) {}
    Animator& scale(f32 to, f32 seconds);
    Animator& fade(f32 to, f32 seconds);
    Animator& move(Vec2 offset, f32 seconds);
    Animator& ease(Ease e);
    Animator& delay(f32 seconds);
    Animator& yoyo();
    Animator& loop();
    Animator& onDone(std::function<void()> fn);

private:
    Animator& start(u8 property, Vec2 to, f32 seconds);
    Canvas* canvas_;
    UiHandle h_;
    i32 slot_ = -1;
};

class ElementRef {
public:
    ElementRef() = default;
    ElementRef(Canvas* canvas, UiHandle handle) : canvas_(canvas), h_(handle) {}

    bool valid() const;
    UiHandle handle() const { return h_; }
    Kind kind() const;
    // Laid-out box in design units (zero before the first layout or for an empty ref).
    Rect box() const;

    void setVisible(bool visible);
    void setEnabled(bool enabled);
    void setStyle(std::string_view style);
    void setOpacity(f32 opacity);
    void setOffset(Vec2 offset);
    void setScale(f32 scale);

    // Adds a child, e.g. root().add<Button>({.id = "ok", .text = "OK"}).
    template <class W>
    typename W::Ref add(const UiSpec& spec) {
        return typename W::Ref(canvas_, addChild(W::kind, spec));
    }

    // Removes this element and its children (they stop drawing and taking touches at once).
    void remove();

    // Animates scale, opacity or offset (visual only: layout never re-runs).
    Animator animate() { return Animator(canvas_, h_); }

protected:
    UiHandle addChild(Kind kind, const UiSpec& spec);

    Canvas* canvas_ = nullptr;
    UiHandle h_;
};

class PanelRef : public ElementRef {
public:
    using ElementRef::ElementRef;
};

class ImageRef : public ElementRef {
public:
    using ElementRef::ElementRef;
    void setSprite(std::string_view sprite);
};

class LabelRef : public ElementRef {
public:
    using ElementRef::ElementRef;
    void setText(std::string_view text);
};

class ButtonRef : public ElementRef {
public:
    using ElementRef::ElementRef;
    void setText(std::string_view text);
    // Bound to the element's id when it has one, else to this element.
    void onClick(std::function<void()> fn);
};

class ToggleRef : public ElementRef {
public:
    using ElementRef::ElementRef;
    void setOn(bool on);
    bool on() const;
    // Called with the new value when the player flips it (not when code calls setOn).
    void onChange(std::function<void(bool)> fn);
};

class SliderRef : public ElementRef {
public:
    using ElementRef::ElementRef;
    void setValue(f32 value);
    f32 value() const;
    // While dragging, each time the (stepped) value changes.
    void onChange(std::function<void(f32)> fn);
    // Once, when the finger lifts: a good moment to save.
    void onCommit(std::function<void(f32)> fn);
};

class ProgressRef : public ElementRef {
public:
    using ElementRef::ElementRef;
    // Clamped to [0, 1]; with `animate` > 0 the bar eases toward it.
    void setValue(f32 value);
    f32 value() const;
    // The value currently drawn (mid-animation it trails value()).
    f32 displayedValue() const;
};

// Widget tags for ElementRef::add<W>() and the typed lookups.
struct Panel { static constexpr Kind kind = Kind::Panel; using Ref = PanelRef; };
struct Image { static constexpr Kind kind = Kind::Image; using Ref = ImageRef; };
struct Label { static constexpr Kind kind = Kind::Label; using Ref = LabelRef; };
struct Button { static constexpr Kind kind = Kind::Button; using Ref = ButtonRef; };
struct Toggle { static constexpr Kind kind = Kind::Toggle; using Ref = ToggleRef; };
struct Slider { static constexpr Kind kind = Kind::Slider; using Ref = SliderRef; };
struct ProgressBar { static constexpr Kind kind = Kind::ProgressBar; using Ref = ProgressRef; };

} // namespace nova::ui
