#include "support/ui_fixture.hpp"

#include <ui/layout.hpp>

#include <vector>

using namespace nova;
using namespace nova::ui;
using nova::testing::UiFixture;

TEST_CASE("Toggle flips and reports the new value") {
    UiFixture fx;
    auto sound = fx.canvas.root().add<Toggle>({.id = "sound", .offset = {100, 100}});
    std::vector<bool> changes;
    sound.onChange([&](bool on) { changes.push_back(on); });
    fx.frame();
    const Vec2 at = sound.box().center() + Vec2{100, 100};
    fx.tap(at);
    CHECK(sound.on());
    fx.tap(at);
    CHECK(!sound.on());
    CHECK(changes == std::vector<bool>{true, false});
}

TEST_CASE("Slider jumps, drags, steps and commits") {
    UiFixture fx;
    auto volume = fx.canvas.root().add<Slider>({.id = "volume", .width = Size::px(400), .height = Size::px(48),
                                                .offset = {100, 100}, .min = 0, .max = 1, .step = 0.25f});
    std::vector<f32> changes, commits;
    volume.onChange([&](f32 v) { changes.push_back(v); });
    volume.onCommit([&](f32 v) { commits.push_back(v); });
    fx.frame();
    fx.press(1, {300, 124});
    fx.frame();
    CHECK(volume.value() == doctest::Approx(0.5f));
    fx.move(1, {350, 124}); // 0.625 snaps back to 0.5: no new change
    fx.frame();
    fx.move(1, {390, 124}); // 0.725 snaps to 0.75
    fx.frame();
    CHECK(volume.value() == doctest::Approx(0.75f));
    fx.move(1, {900, 124}); // dragging past the end clamps (and never cancels)
    fx.frame();
    fx.move(1, {390, 124});
    fx.frame();
    fx.release(1, {390, 124});
    fx.frame();
    CHECK(changes == std::vector<f32>{0.5f, 0.75f, 1.f, 0.75f});
    CHECK(commits == std::vector<f32>{0.75f});
}

TEST_CASE("Progress bar animates toward its target") {
    UiFixture fx;
    auto bar = fx.canvas.root().add<ProgressBar>({.id = "hp", .animate = 0.5f});
    fx.frame();
    bar.setValue(1);
    fx.frame(0.25f);
    CHECK(bar.displayedValue() > 0.f);
    CHECK(bar.displayedValue() < 1.f);
    fx.frame(0.35f);
    CHECK(bar.displayedValue() == doctest::Approx(1.f));
    bar.setValue(2); // clamped
    fx.frame(1);
    CHECK(bar.displayedValue() == doctest::Approx(1.f));
    auto preset = fx.canvas.root().add<ProgressBar>({.value = 0.3f, .animate = 0.5f});
    CHECK(preset.displayedValue() == doctest::Approx(0.3f)); // starts at its value, no animation
}

TEST_CASE("Animations move, scale and fade without layout") {
    UiFixture fx;
    Style solid;
    solid.background.kind = Background::Kind::Shape;
    fx.shared->theme.set("solid", solid);
    auto box = fx.canvas.root().add<Panel>({.style = "solid", .width = Size::px(100), .height = Size::px(100),
                                            .anchor = Anchor::Center});
    fx.frame();
    const Rect laidOut = box.box();
    box.animate().move({0, -20}, 0.2f);
    fx.frame(0.1f);
    CHECK(layoutCanvas(fx.canvas.impl().store, {fx.renderer->visibleRect(), fx.renderer->safeArea(),
                                                fx.renderer->pixelScale()}, nullptr, nullptr) == 0);
    fx.frame(0.15f);
    CHECK(box.box() == laidOut);
    CHECK(fx.canvas.impl().drawList.layers[0].sprites[0].xform.apply({0, 0}).y == doctest::Approx(laidOut.y - 20));

    box.animate().fade(0, 0.1f).ease(Ease::Linear);
    fx.frame(0.05f);
    const u8 half = fx.canvas.impl().drawList.layers[0].sprites[0].color.a;
    CHECK(half > 100);
    CHECK(half < 155);
    bool done = false;
    box.animate().scale(2, 0.1f).onDone([&] { done = true; });
    fx.frame(0.2f);
    CHECK(done);
    CHECK(visualBox(fx.canvas.impl().store, box.handle()).w == doctest::Approx(200));
}

TEST_CASE("Yoyo and loop run until the element goes away") {
    UiFixture fx;
    auto box = fx.canvas.root().add<Panel>({.width = Size::px(100), .height = Size::px(100)});
    box.animate().scale(1.2f, 0.1f).yoyo().loop();
    for (int i = 0; i < 30; ++i) fx.frame(0.05f);
    u32 busy = 0;
    for (const auto& a : fx.canvas.impl().anims) busy += a.active;
    CHECK(busy == 1); // still looping
    box.remove();
    fx.frame();
    busy = 0;
    for (const auto& a : fx.canvas.impl().anims) busy += a.active;
    CHECK(busy == 0);
}

TEST_CASE("Remembered values follow the id") {
    UiFixture fx;
    auto title = fx.canvas.root().add<Label>({.id = "title"});
    title.setText("A");
    title.remove();
    fx.frame();
    fx.canvas.root().add<Label>({.id = "title"});
    CHECK(fx.canvas.rememberedTextForTests("title") == "A");
    auto t = fx.canvas.root().add<Toggle>({.id = "music", .offset = {0, 600}});
    fx.frame();
    fx.tap(t.box().center() + Vec2{0, 600});
    CHECK(fx.canvas.impl().remembered.at("music").on == std::optional<bool>(true));
}
