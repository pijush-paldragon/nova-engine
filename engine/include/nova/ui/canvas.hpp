#pragma once

#include <nova/ui/element.hpp>

#include <functional>
#include <memory>
#include <string_view>

namespace nova::ui {

// An open popup: a layer above the screen (and earlier popups) with a backdrop that blocks every
// touch beneath it. Empty once closed.
class PopupRef {
public:
    PopupRef() = default;
    PopupRef(Canvas* canvas, u32 token) : canvas_(canvas), token_(token) {}

    bool valid() const;
    // Starts the exit transition; onClose runs when it has finished and the popup is gone.
    void close();
    void onClose(std::function<void()> fn);

    ElementRef root();
    ElementRef element(std::string_view id);
    PanelRef panel(std::string_view id);
    ImageRef image(std::string_view id);
    LabelRef label(std::string_view id);
    ButtonRef button(std::string_view id);
    ToggleRef toggle(std::string_view id);
    SliderRef slider(std::string_view id);
    ProgressRef progress(std::string_view id);

private:
    UiHandle find(std::string_view id, int kind) const; // kind -1: any
    Canvas* canvas_ = nullptr;
    u32 token_ = 0; // identifies the popup's layer, also across live reloads of its file
};

// A scene's UI: a tree of elements drawn above the scene's world. Reached with Scene::ui().
// Lookups by id never fail hard: a missing id or wrong kind returns an empty reference (and is
// logged once in dev builds).
class Canvas {
public:
    Canvas();
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    ElementRef root();

    // Replaces the screen with a layout file (asset-root relative, e.g. "ui/menu.json"). If the
    // file can't be read or parsed, the current screen stays (and dev builds log why).
    void load(std::string_view file);

    ElementRef element(std::string_view id);
    PanelRef panel(std::string_view id);
    ImageRef image(std::string_view id);
    LabelRef label(std::string_view id);
    ButtonRef button(std::string_view id);
    ToggleRef toggle(std::string_view id);
    SliderRef slider(std::string_view id);
    ProgressRef progress(std::string_view id);

    // Opens a popup from a layout file, or with a code-built root (add children to popup.root()).
    // The popup is centred unless its root sets an anchor. Android back / Escape closes the top
    // popup before the scene sees backPressed().
    PopupRef push(std::string_view file, PopupOptions options = {});
    PopupRef push(const UiSpec& root, PopupOptions options = {});
    // Closes the top popup (if any).
    void pop();
    // Open popups, not counting ones already closing.
    u32 popupCount() const;

    // Runs fn when the button with this id is clicked. Bound by id, so it survives the element
    // being removed and re-added (or the layout file reloading).
    void onClick(std::string_view id, std::function<void()> fn);

    // Elements alive in this canvas, root included (removed ones count until the frame ends).
    u32 elementCount() const;

    // Dev builds with live reload: the first error of the last failed edit (empty when fine).
    std::string_view devError() const;

    // Test hook: the text last set through a reference for this id.
    std::string_view rememberedTextForTests(std::string_view id) const;

    // Engine use only.
    struct Impl;
    Impl& impl() { return *impl_; }
    const Impl& impl() const { return *impl_; }

private:
    UiHandle findTyped(std::string_view id, Kind kind);

    std::unique_ptr<Impl> impl_;
};

} // namespace nova::ui
