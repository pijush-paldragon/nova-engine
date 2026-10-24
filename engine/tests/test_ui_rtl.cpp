#include "support/ui_fixture.hpp"

#include <text/localization_impl.hpp>
#include <ui/layout.hpp>

#include <string>

using namespace nova;
using namespace nova::ui;
using nova::testing::UiFixture;

namespace {

std::optional<std::string> readTestText(std::string_view path) {
    std::string full = std::string(NOVA_TEST_DATA_DIR) + std::string(path);
    size_t size = 0;
    void* data = SDL_LoadFile(full.c_str(), &size);
    if (!data) return std::nullopt;
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    return text;
}

struct RtlFixture : UiFixture {
    RtlFixture() {
        text->lang().impl().setReader(readTestText);
        assets->setLocalization(&text->lang());
        REQUIRE(text->lang().setLanguage("en"));
        Style def = shared->theme.resolve("default", VisualState::Normal);
        def.font = std::string("fonts/Fredoka.ttf");
        shared->theme.set("default", def);
    }
    void rtl(bool on) {
        REQUIRE(text->lang().setLanguage(on ? "ar" : "en"));
        REQUIRE(text->lang().rightToLeft() == on);
        frame();
    }
    const Element& el(const ElementRef& r) { return *canvas.impl().store.get(r.handle()); }
    Rect screen() { return canvas.root().box(); }
};

} // namespace

TEST_CASE("Rows reverse in right-to-left languages") {
    RtlFixture fx;
    auto row = fx.canvas.root().add<Panel>({.width = Size::px(600), .height = Size::px(100), .layout = LayoutMode::Row});
    auto a = row.add<Panel>({.width = Size::px(100), .height = Size::px(100)});
    auto b = row.add<Panel>({.width = Size::px(150), .height = Size::px(100)});
    fx.frame();
    CHECK(row.box().x == doctest::Approx(fx.screen().x));
    CHECK(a.box().x == doctest::Approx(row.box().x));
    CHECK(b.box().x == doctest::Approx(a.box().right()));

    fx.rtl(true);
    CHECK(row.box().right() == doctest::Approx(fx.screen().right()));
    CHECK(a.box().right() == doctest::Approx(row.box().right()));
    CHECK(b.box().right() == doctest::Approx(a.box().x));

    fx.rtl(false);
    CHECK(a.box().x == doctest::Approx(row.box().x));
}

TEST_CASE("Anchors and padding mirror") {
    RtlFixture fx;
    auto p = fx.canvas.root().add<Panel>(
        {.width = Size::px(400), .height = Size::px(200), .offset = {20, 10}, .padding = {0, 0, 0, 30}});
    auto inner = p.add<Panel>({.width = Size::fill(), .height = Size::px(50)});
    auto corner = fx.canvas.root().add<Panel>(
        {.width = Size::px(80), .height = Size::px(80), .anchor = Anchor::BottomLeft});
    auto column = fx.canvas.root().add<Panel>({.width = Size::px(300), .height = Size::px(300), .anchor = Anchor::Center,
                                               .layout = LayoutMode::Column, .align = Align::Start});
    auto item = column.add<Panel>({.width = Size::px(100), .height = Size::px(40)});
    ElementStore& store = fx.canvas.impl().store;
    fx.frame();
    CHECK(inner.box().x == doctest::Approx(p.box().x + 30));
    CHECK(visualBox(store, p.handle()).x == doctest::Approx(p.box().x + 20));
    CHECK(item.box().x == doctest::Approx(column.box().x));

    fx.rtl(true);
    CHECK(p.box().right() == doctest::Approx(fx.screen().right()));
    CHECK(inner.box().x == doctest::Approx(p.box().x));
    CHECK(inner.box().right() == doctest::Approx(p.box().right() - 30));
    const Rect shown = visualBox(store, p.handle());
    CHECK(shown.x == doctest::Approx(p.box().x - 20)); // offset.x mirrors
    CHECK(shown.y == doctest::Approx(p.box().y + 10));
    CHECK(corner.box().right() == doctest::Approx(fx.screen().right()));
    CHECK(corner.box().bottom() == doctest::Approx(fx.screen().bottom()));
    CHECK(item.box().right() == doctest::Approx(column.box().right())); // start alignment is the right edge
}

TEST_CASE("mirror false keeps an element's subtree as authored") {
    RtlFixture fx;
    auto row = fx.canvas.root().add<Panel>(
        {.width = Size::px(600), .height = Size::px(100), .layout = LayoutMode::Row, .mirror = false});
    auto a = row.add<Panel>({.width = Size::px(100), .height = Size::px(100)});
    auto b = row.add<Panel>({.width = Size::px(150), .height = Size::px(100), .offset = {5, 0}});
    ElementStore& store = fx.canvas.impl().store;
    fx.rtl(true);
    CHECK(row.box().x == doctest::Approx(fx.screen().x));
    CHECK(a.box().x == doctest::Approx(row.box().x));
    CHECK(b.box().x == doctest::Approx(a.box().right()));
    CHECK(visualBox(store, b.handle()).x == doctest::Approx(b.box().x + 5));
}

TEST_CASE("spriteRtl replaces the sprite") {
    RtlFixture fx;
    auto img = fx.canvas.root().add<Image>({.sprite = "sprites/ball", .spriteRtl = "sprites/paddle"});
    const Rect ball = fx.assets->atlas("sprites").region("ball").src;
    const Rect paddle = fx.assets->atlas("sprites").region("paddle").src;
    REQUIRE(!(ball.w == paddle.w && ball.h == paddle.h));
    fx.frame();
    CHECK(fx.el(img).region.src.w == doctest::Approx(ball.w));
    fx.rtl(true);
    CHECK(fx.el(img).region.src.w == doctest::Approx(paddle.w));
    CHECK(fx.el(img).region.src.h == doctest::Approx(paddle.h));
    fx.rtl(false);
    CHECK(fx.el(img).region.src.w == doctest::Approx(ball.w));
}

TEST_CASE("Switching direction relays every layer") {
    RtlFixture fx;
    PopupRef popup = fx.canvas.push(UiSpec{.width = Size::px(100), .height = Size::px(100), .anchor = Anchor::Left},
                                    PopupOptions{.dim = 0, .enter = Transition::None, .exit = Transition::None});
    fx.frame();
    CHECK(popup.root().box().x == doctest::Approx(fx.screen().x));
    fx.rtl(true);
    CHECK(popup.root().box().right() == doctest::Approx(fx.screen().right()));
}

TEST_CASE("Language switch re-resolves UI fonts") {
    RtlFixture fx;
    auto label = fx.canvas.root().add<Label>({.text = "Play"});
    fx.frame();
    const FontHandle en = fx.el(label).font;
    REQUIRE(en.valid());
    fx.rtl(true);
    CHECK(fx.el(label).font.valid());
    CHECK(!(fx.el(label).font == en)); // the Arabic font chain (path@size#ar)
}

TEST_CASE("Labels align to the start of their direction") {
    RtlFixture fx;
    auto mirrored = fx.canvas.root().add<Label>({.text = "Hi", .width = Size::px(400)});
    auto fixed = fx.canvas.root().add<Label>({.text = "Hi", .width = Size::px(400), .offset = {0, 200}, .mirror = false});
    fx.rtl(true);
    const DrawLayer& layer = fx.canvas.impl().drawList.layers[0];
    REQUIRE(layer.texts.size() == 2);
    CHECK(layer.texts[0].align == TextAlignH::Right);
    CHECK(layer.texts[1].align == TextAlignH::Left);
    CHECK(fx.el(mirrored).spec.textAlign == TextAlignH::Start);
    (void)fixed;
}
