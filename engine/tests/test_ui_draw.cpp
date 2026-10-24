#include "support/ui_fixture.hpp"

#include <nova/core/alloc_stats.hpp>
#include <ui/layout.hpp>

#include <cstdio>

using namespace nova;
using namespace nova::ui;
using nova::testing::LogCapture;
using nova::testing::UiFixture;

namespace {

Style withFont(Style s) {
    s.font = "fonts/Fredoka.ttf";
    s.fontSize = 32;
    return s;
}

} // namespace

TEST_CASE("Theme resolves extends and states") {
    LogCapture logs;
    Theme theme = Theme::builtin();
    Style base;
    base.font = "fonts/Fredoka.ttf";
    base.textColor = Color::fromHex(0x112233FF);
    theme.set("base", base);
    Style child;
    child.extends = "base";
    child.background.kind = Background::Kind::Shape;
    child.background.radius = 16;
    child.pressed.tint = Color::fromHex(0xD9D9D9FF);
    theme.set("child", child);
    const Style& normal = theme.resolve("child", VisualState::Normal);
    CHECK(normal.font == "fonts/Fredoka.ttf"); // inherited
    CHECK(normal.background.radius == 16);
    CHECK(normal.tint == Color::White);
    CHECK(theme.resolve("child", VisualState::Pressed).tint == Color::fromHex(0xD9D9D9FF));
    CHECK(theme.resolve("child", VisualState::Disabled).opacity == doctest::Approx(0.5f)); // built-in default

    Style a, b;
    a.extends = "b";
    b.extends = "a";
    theme.set("a", a);
    theme.set("b", b);
    theme.resolve("a", VisualState::Normal); // terminates
    CHECK(logs.count("style extends cycle") == 1);
    theme.resolve("nope", VisualState::Normal);
    CHECK(logs.count("ui: unknown style \"nope\"") == 1);
}

TEST_CASE("Nine-slice emits nine quads with fixed corners") {
    UiFixture fx;
    Style card;
    card.background.kind = Background::Kind::Shape;
    card.background.radius = 24;
    fx.shared->theme.set("card", card);
    fx.canvas.root().add<Panel>({.style = "card", .width = Size::px(200), .height = Size::px(80)});
    fx.frame();
    const DrawList& list = fx.canvas.impl().drawList;
    REQUIRE(list.layers.size() >= 1);
    const auto& quads = list.layers[0].sprites;
    REQUIRE(quads.size() == 9);
    CHECK(quads[0].size == Vec2{24, 24});        // top-left corner
    CHECK(quads[4].size == Vec2{152, 32});       // centre
    CHECK(quads[8].xform.apply({0, 0}) == Vec2{176, 56}); // bottom-right corner origin
}

TEST_CASE("Image fit modes") {
    UiFixture fx; // sprites/paddle is 160x32
    auto stretch = fx.canvas.root().add<Image>({.sprite = "sprites/paddle", .width = Size::px(200), .height = Size::px(200)});
    auto contain = fx.canvas.root().add<Image>({.sprite = "sprites/paddle", .width = Size::px(200),
                                                .height = Size::px(200), .imageFit = ImageFit::Contain});
    auto cover = fx.canvas.root().add<Image>({.sprite = "sprites/paddle", .width = Size::px(200),
                                              .height = Size::px(200), .imageFit = ImageFit::Cover});
    auto natural = fx.canvas.root().add<Image>({.sprite = "sprites/paddle"});
    fx.frame();
    const auto& quads = fx.canvas.impl().drawList.layers[0].sprites;
    REQUIRE(quads.size() == 4);
    CHECK(quads[0].size == Vec2{200, 200});
    CHECK(quads[1].size == Vec2{200, 40});
    CHECK(quads[1].xform.apply({0, 0}).y == 80); // centred vertically
    CHECK(quads[2].size == Vec2{200, 200});
    CHECK(quads[2].src.w == doctest::Approx(32)); // cropped to the box's aspect
    CHECK(quads[2].src.x == doctest::Approx(2 + 64));
    CHECK(natural.box().w == 160); // fit = sprite size
    (void)stretch;
    (void)contain;
    (void)cover;
}

TEST_CASE("Label text sits above its button in the same layer") {
    UiFixture fx;
    fx.shared->theme.set("button", withFont(fx.shared->theme.resolve("button", VisualState::Normal)));
    fx.canvas.root().add<Button>({.text = "Play", .width = Size::px(300), .height = Size::px(100)});
    fx.canvas.root().add<Label>({.style = "button", .text = "Hi"});
    fx.frame();
    const DrawLayer& layer = fx.canvas.impl().drawList.layers[0];
    CHECK(layer.sprites.size() >= 1); // button background
    CHECK(layer.texts.size() == 2);   // drawn after this layer's sprites (uiDraw submits sprites first)
    CHECK(layer.texts[0].box.w > 0);
}

TEST_CASE("Opacity and scale multiply down the tree") {
    UiFixture fx;
    Style solid;
    solid.background.kind = Background::Kind::Shape;
    fx.shared->theme.set("solid", solid);
    auto parent = fx.canvas.root().add<Panel>({.style = "solid", .width = Size::px(200), .height = Size::px(200),
                                               .opacity = 0.5f});
    auto child = parent.add<Panel>({.style = "solid", .width = Size::px(100), .height = Size::px(100), .opacity = 0.5f});
    fx.frame();
    const auto& quads = fx.canvas.impl().drawList.layers[0].sprites;
    REQUIRE(quads.size() == 2);
    CHECK(quads[1].color.a == 64); // 255 * 0.25, rounded

    parent.setScale(0.5f);
    fx.frame();
    const auto& scaled = fx.canvas.impl().drawList.layers[0].sprites;
    // Child (0,0)-(100,100) scaled by 0.5 around the parent's centre (100,100).
    CHECK(scaled[1].xform.apply({0, 0}) == Vec2{50, 50});
    CHECK(scaled[1].xform.apply(scaled[1].size) == Vec2{100, 100});
    CHECK(visualBox(fx.canvas.impl().store, child.handle()) == Rect{50, 50, 50, 50});
}

TEST_CASE("Label growth relayouts its row and allocates only on growth") {
    UiFixture fx;
    fx.shared->theme.set("default", withFont(fx.shared->theme.resolve("default", VisualState::Normal)));
    auto row = fx.canvas.root().add<Panel>({.layout = LayoutMode::Row, .gap = 10});
    auto score = row.add<Label>({.text = "9"});
    auto after = row.add<Label>({.text = "x"});
    fx.frame();
    const f32 x1 = after.box().x;
    score.setText("10");
    fx.frame();
    CHECK(after.box().x > x1);

    char buf[16];
    for (int i = 11; i < 20; ++i) { // warm up same-length strings
        std::snprintf(buf, sizeof buf, "%d", i);
        score.setText(buf);
        fx.frame();
    }
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    for (int i = 20; i < 99; ++i) {
        std::snprintf(buf, sizeof buf, "%d", i);
        score.setText(buf);
        fx.frame();
    }
    CHECK(memory::allocationCount() == before);
    score.setText("100000000000000000000000000000000"); // growth may allocate; must not crash
    fx.frame();
}

TEST_CASE("Missing font draws nothing and logs once") {
    LogCapture logs;
    UiFixture fx;
    Style bare;
    fx.shared->theme.set("bare", bare);
    fx.canvas.root().add<Label>({.style = "bare", .text = "hello"});
    fx.frame();
    fx.frame();
    CHECK(fx.canvas.impl().drawList.layers[0].texts.empty());
    CHECK(logs.count("ui: style \"bare\" has no font") == 1);
}

TEST_CASE("First text change on an id'd label allocates nothing") {
    UiFixture fx;
    fx.shared->theme.set("default", withFont(fx.shared->theme.resolve("default", VisualState::Normal)));
    fx.canvas.root().add<Label>({.id = "score", .text = "Score 0"});
    for (int i = 0; i < 3; ++i) fx.frame();
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    fx.canvas.label("score").setText("Score 10"); // never changed before: no bookkeeping may allocate
    fx.frame();
    CHECK(memory::allocationCount() == before);
}

