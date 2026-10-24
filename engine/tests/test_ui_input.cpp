#include "support/ui_fixture.hpp"

using namespace nova;
using namespace nova::ui;
using nova::testing::UiFixture;

namespace {

UiSpec box(const char* id, Vec2 pos, Vec2 size) {
    return UiSpec{.id = id, .width = Size::px(size.x), .height = Size::px(size.y), .offset = pos};
}

} // namespace

TEST_CASE("Button clicks on release inside, not on press") {
    UiFixture fx;
    fx.canvas.root().add<Button>(box("ok", {100, 100}, {200, 100}));
    int clicks = 0;
    fx.canvas.onClick("ok", [&] { ++clicks; });
    fx.frame();
    fx.press(1, {200, 150});
    fx.frame();
    CHECK(clicks == 0);
    fx.release(1, {200, 150});
    fx.frame();
    CHECK(clicks == 1);
}

TEST_CASE("Dragging 16 px off a button cancels it") {
    UiFixture fx;
    fx.canvas.root().add<Button>(box("ok", {100, 100}, {200, 100})); // right edge at x = 300
    int clicks = 0;
    fx.canvas.onClick("ok", [&] { ++clicks; });
    fx.frame();
    fx.press(1, {200, 150});
    fx.frame();
    fx.move(1, {317, 150}); // 17 px past the edge
    fx.frame();
    fx.move(1, {200, 150}); // back inside: stays cancelled
    fx.frame();
    fx.release(1, {200, 150});
    fx.frame();
    CHECK(clicks == 0);

    fx.press(2, {200, 150});
    fx.frame();
    fx.move(2, {315, 150}); // 15 px past: still pressed
    fx.frame();
    fx.release(2, {315, 150});
    fx.frame();
    CHECK(clicks == 1);
}

TEST_CASE("Topmost element wins and owns the touch") {
    UiFixture fx;
    fx.canvas.root().add<Button>(box("under", {100, 100}, {200, 100}));
    fx.canvas.root().add<Button>(box("over", {150, 100}, {200, 100}));
    int under = 0, over = 0;
    fx.canvas.onClick("under", [&] { ++under; });
    fx.canvas.onClick("over", [&] { ++over; });
    fx.frame();
    fx.tap({200, 150});
    CHECK(over == 1);
    CHECK(under == 0);
}

TEST_CASE("Two fingers press two buttons") {
    UiFixture fx;
    fx.canvas.root().add<Button>(box("a", {0, 0}, {200, 100}));
    fx.canvas.root().add<Button>(box("b", {300, 0}, {200, 100}));
    int a = 0, b = 0;
    fx.canvas.onClick("a", [&] { ++a; });
    fx.canvas.onClick("b", [&] { ++b; });
    fx.frame();
    fx.press(1, {50, 50});
    fx.press(2, {350, 50});
    fx.frame();
    fx.release(1, {50, 50});
    fx.release(2, {350, 50});
    fx.frame();
    CHECK(a == 1);
    CHECK(b == 1);
}

TEST_CASE("Disabled buttons ignore touches") {
    UiFixture fx;
    auto ok = fx.canvas.root().add<Button>(box("ok", {100, 100}, {200, 100}));
    int clicks = 0;
    fx.canvas.onClick("ok", [&] { ++clicks; });
    ok.setEnabled(false);
    fx.frame();
    fx.press(1, {200, 150});
    fx.beginUi();
    CHECK(fx.input.pointer(0)->consumedByUi); // a disabled button still shields gameplay
    fx.endFrame();
    fx.release(1, {200, 150});
    fx.frame();
    CHECK(clicks == 0);
}

TEST_CASE("UI taps are hidden from gameplay input") {
    UiFixture fx;
    fx.canvas.root().add<Button>(box("ok", {100, 100}, {200, 100}));
    fx.frame();
    fx.press(1, {200, 150});
    fx.release(1, {200, 150}); // a quick tap within one frame
    fx.beginUi();
    CHECK(fx.input.pointer(0)->consumedByUi);
    CHECK(!fx.input.tapped());
    CHECK(fx.input.primary() == nullptr);
    fx.endFrame();

    fx.press(2, {600, 900}); // empty space
    fx.release(2, {600, 900});
    fx.beginUi();
    CHECK(fx.input.tapped());
    CHECK(fx.input.primary() != nullptr);
    fx.endFrame();
}

TEST_CASE("Non-interactive UI passes touches through unless it blocks") {
    UiFixture fx;
    fx.canvas.root().add<Label>(box("label", {100, 100}, {200, 100}));
    UiSpec wall = box("wall", {100, 400}, {200, 100});
    wall.blocksInput = true;
    fx.canvas.root().add<Panel>(wall);
    fx.frame();
    fx.press(1, {200, 150});
    fx.release(1, {200, 150});
    fx.beginUi();
    CHECK(fx.input.tapped());
    fx.endFrame();
    fx.press(2, {200, 450});
    fx.release(2, {200, 450});
    fx.beginUi();
    CHECK(!fx.input.tapped());
    fx.endFrame();
}

TEST_CASE("Bindings survive removing and re-adding the element") {
    UiFixture fx;
    auto ok = fx.canvas.root().add<Button>(box("ok", {100, 100}, {200, 100}));
    int clicks = 0;
    fx.canvas.onClick("ok", [&] { ++clicks; });
    ok.remove();
    fx.frame();
    fx.canvas.root().add<Button>(box("ok", {100, 100}, {200, 100}));
    fx.frame();
    fx.tap({200, 150});
    CHECK(clicks == 1);

    // A ref binding on an element without an id follows that element.
    auto anon = fx.canvas.root().add<Button>(box("", {100, 400}, {200, 100}));
    int anonClicks = 0;
    anon.onClick([&] { ++anonClicks; });
    fx.frame();
    fx.tap({200, 450});
    CHECK(anonClicks == 1);
}

TEST_CASE("Callback that removes its own panel is safe") {
    UiFixture fx;
    auto panel = fx.canvas.root().add<Panel>({.id = "dialog", .width = Size::px(400), .height = Size::px(400)});
    panel.add<Button>(box("close", {100, 100}, {200, 100}));
    int clicks = 0;
    fx.canvas.onClick("close", [&] {
        ++clicks;
        fx.canvas.panel("dialog").remove();
        fx.canvas.onClick("close", [] {}); // rebinding the running callback is safe too
    });
    fx.frame();
    fx.tap({200, 150});
    CHECK(clicks == 1);
    CHECK(!fx.canvas.panel("dialog").valid());
    CHECK(fx.canvas.elementCount() == 1);
}
