#include "support/ui_fixture.hpp"

#include <SDL3/SDL_events.h>

#include <cmath>

using namespace nova;
using namespace nova::ui;
using nova::testing::UiFixture;

namespace {

// A centred 400x400 popup with a "close" button at its top-left corner area.
PopupRef openDialog(UiFixture& fx, PopupOptions options = {}) {
    PopupRef popup = fx.canvas.push(UiSpec{.width = Size::px(400), .height = Size::px(400)}, options);
    popup.root().add<Button>({.id = "close", .width = Size::px(200), .height = Size::px(100)});
    return popup;
}

constexpr Vec2 kCloseAt{160 + 100, 440 + 50}; // popup at (160, 440): button centre
constexpr Vec2 kOutside{60, 100};

void back(UiFixture& fx) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = SDL_SCANCODE_AC_BACK;
    fx.input.handleEvent(e, fx.renderer->fit());
}

void settle(UiFixture& fx) {
    for (int i = 0; i < 20; ++i) fx.frame(0.05f);
}

} // namespace

TEST_CASE("Popup blocks touches below it") {
    UiFixture fx;
    fx.canvas.root().add<Button>({.id = "base", .width = Size::px(720), .height = Size::px(1280)});
    int baseClicks = 0;
    fx.canvas.onClick("base", [&] { ++baseClicks; });
    openDialog(fx);
    settle(fx);
    fx.press(1, kOutside);
    fx.release(1, kOutside);
    fx.beginUi();
    CHECK(!fx.input.tapped());
    fx.endFrame();
    fx.frame();
    CHECK(baseClicks == 0);
}

TEST_CASE("Backdrop closes only when asked") {
    UiFixture fx;
    PopupRef stay = openDialog(fx);
    settle(fx);
    fx.tap(kOutside);
    settle(fx);
    CHECK(stay.valid());
    CHECK(fx.canvas.popupCount() == 1);

    stay.close();
    settle(fx);
    PopupRef go = openDialog(fx, {.closeOnBackdrop = true});
    int closed = 0;
    go.onClose([&] { ++closed; });
    settle(fx);
    fx.tap({160 + 300, 440 + 300}); // inside the popup, not on a button: stays open
    settle(fx);
    CHECK(fx.canvas.popupCount() == 1);
    fx.tap(kOutside);
    CHECK(fx.canvas.popupCount() == 0); // closing at once (exit transition still running)
    CHECK(closed == 0);
    settle(fx);
    CHECK(closed == 1);
    CHECK(!go.valid());
}

TEST_CASE("Back closes the top popup before the scene sees it") {
    UiFixture fx;
    openDialog(fx);
    openDialog(fx);
    settle(fx);
    back(fx);
    fx.beginUi();
    CHECK(fx.canvas.popupCount() == 1);
    CHECK(!fx.input.backPressed());
    fx.endFrame();
    settle(fx);
    back(fx);
    fx.beginUi();
    CHECK(fx.canvas.popupCount() == 0);
    fx.endFrame();
    settle(fx);
    back(fx);
    fx.beginUi();
    CHECK(fx.input.backPressed());
    fx.endFrame();
}

TEST_CASE("Touches are ignored during transitions") {
    UiFixture fx;
    openDialog(fx, {.duration = 0.2f});
    int clicks = 0;
    fx.canvas.onClick("close", [&] { ++clicks; });
    fx.frame(0.02f);
    fx.press(1, kCloseAt);
    fx.frame(0.03f);
    fx.release(1, kCloseAt);
    fx.frame(0.01f);
    CHECK(clicks == 0);
    for (int i = 0; i < 10; ++i) fx.frame(0.05f);
    fx.tap(kCloseAt);
    CHECK(clicks == 1);
}

TEST_CASE("Pop transition scales from 0.85 with OutBack") {
    UiFixture fx;
    PopupRef popup = openDialog(fx, {.enter = Transition::Pop, .duration = 0.2f});
    const Element* root = fx.canvas.impl().store.get(popup.root().handle());
    fx.frame(0);
    CHECK(root->scale == doctest::Approx(0.85f));
    f32 peak = 0;
    for (int i = 0; i < 20; ++i) {
        fx.frame(0.01f);
        peak = std::max(peak, fx.canvas.impl().store.get(popup.root().handle())->scale);
    }
    CHECK(peak > 1.f);
    fx.frame(0.1f);
    CHECK(fx.canvas.impl().store.get(popup.root().handle())->scale == doctest::Approx(1.f));
}

TEST_CASE("Touch that began before a popup opened never clicks inside it") {
    UiFixture fx;
    fx.frame();
    fx.press(1, kCloseAt); // empty base area
    fx.frame();
    openDialog(fx, {.enter = Transition::None});
    int clicks = 0;
    fx.canvas.onClick("close", [&] { ++clicks; });
    fx.frame();
    fx.release(1, kCloseAt);
    fx.frame();
    CHECK(clicks == 0);

    // A finger already holding a base button when a popup opens doesn't click it either.
    fx.canvas.root().add<Button>({.id = "base", .width = Size::px(100), .height = Size::px(100)});
    int baseClicks = 0;
    fx.canvas.onClick("base", [&] { ++baseClicks; });
    fx.canvas.pop();
    settle(fx);
    fx.press(2, {50, 50});
    fx.frame();
    openDialog(fx, {.enter = Transition::None});
    fx.frame();
    fx.release(2, {50, 50});
    fx.frame();
    CHECK(baseClicks == 0);
}

TEST_CASE("Resize re-lays out every layer") {
    UiFixture fx;
    PopupRef popup = openDialog(fx);
    settle(fx);
    CHECK(popup.root().box() == Rect{160, 440, 400, 400});
    SDL_SetWindowSize(fx.window, 640, 360); // landscape
    fx.renderer->onResize();
    settle(fx);
    const Rect visible = fx.renderer->visibleRect();
    const f32 onePixel = 1 / fx.renderer->pixelScale(); // boxes snap to device pixels
    CHECK(visible.w > 720);                              // landscape: the screen got wider
    CHECK(std::fabs(popup.root().box().x - (visible.x + (visible.w - 400) * 0.5f)) <= onePixel);
    CHECK(std::fabs(popup.root().box().y - (visible.y + (visible.h - 400) * 0.5f)) <= onePixel);
}

TEST_CASE("Callback that pops its own popup is safe") {
    UiFixture fx;
    PopupRef popup = openDialog(fx, {.enter = Transition::None});
    int closed = 0;
    popup.onClose([&] { ++closed; });
    fx.canvas.onClick("close", [&] { fx.canvas.pop(); });
    fx.frame();
    fx.tap(kCloseAt);
    settle(fx);
    CHECK(closed == 1);
    CHECK(fx.canvas.popupCount() == 0);
    CHECK(fx.canvas.elementCount() == 1);
}
