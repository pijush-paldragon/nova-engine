#include "support/ui_fixture.hpp"

#include <ui/loader.hpp>

#include <SDL3/SDL_iostream.h>

#include <string>

using namespace nova;
using namespace nova::ui;
using nova::testing::LogCapture;
using nova::testing::UiFixture;

namespace {

// Reads layout files from engine/tests/data/ui/.
std::optional<std::string> readTestFile(std::string_view path) {
    std::string full = std::string(NOVA_TEST_DATA_DIR) + "ui/" + std::string(path);
    size_t size = 0;
    void* data = SDL_LoadFile(full.c_str(), &size);
    if (!data) return std::nullopt;
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    return text;
}

const NodeSpec& node(const std::vector<NodeSpec>& nodes, std::string_view id) {
    for (const NodeSpec& n : nodes) {
        if (n.spec.id == id) return n;
    }
    FAIL("no node " << std::string(id));
    return nodes.front();
}

bool hasError(const LoadResult& r, std::string_view text) {
    for (const std::string& e : r.errors) {
        if (e == text) return true;
    }
    return false;
}

} // namespace

TEST_CASE("Every layout field parses") {
    std::vector<NodeSpec> nodes;
    LoadResult r = loadLayout("menu.json", readTestFile, nullptr, nodes);
    REQUIRE(r.ok);
    CHECK(r.errors.empty());
    CHECK(r.enter == Transition::Pop);
    CHECK(r.exit == Transition::Fade);
    REQUIRE(nodes.size() == 8);
    const UiSpec& menu = nodes[0].spec;
    CHECK(nodes[0].kind == Kind::Panel);
    CHECK(nodes[0].parent == -1);
    CHECK(menu.layout == LayoutMode::Column);
    CHECK(menu.gap == 12);
    CHECK(menu.padding == Insets{1, 2, 3, 4});
    CHECK(menu.justify == Justify::SpaceBetween);
    CHECK(menu.align == Align::Stretch);
    CHECK(menu.safeArea);

    const NodeSpec& title = node(nodes, "title");
    CHECK(title.kind == Kind::Label);
    CHECK(title.parent == 0);
    CHECK(title.spec.style == "hud");
    CHECK(title.spec.text == "Hello");
    CHECK(title.spec.textAlign == TextAlignH::Center);
    CHECK(title.spec.width == Size::fill());
    CHECK(title.spec.height == Size::fit());
    CHECK(title.spec.minWidth == 10);
    CHECK(title.spec.maxWidth == 500);
    CHECK(title.spec.minHeight == 5);
    CHECK(title.spec.maxHeight == 90);

    const UiSpec& play = node(nodes, "play").spec;
    CHECK(node(nodes, "play").kind == Kind::Button);
    CHECK(play.icon == "sprites/ball");
    CHECK(play.width == Size::px(120));
    CHECK(play.height == Size::fill(2));
    CHECK(play.anchor == Anchor::BottomRight);
    CHECK(play.offset == Vec2{4, -6});
    CHECK(!play.visible);
    CHECK(!play.enabled);
    CHECK(play.opacity == 0.5f);
    CHECK(play.blocksInput);

    const UiSpec& logo = node(nodes, "logo").spec;
    CHECK(logo.sprite == "sprites/paddle");
    CHECK(logo.imageFit == ImageFit::Contain);
    CHECK(logo.slice == Insets::all(6));
    CHECK(logo.tint == Color::fromHex(0xFF000080));

    CHECK(node(nodes, "sound").spec.look == ToggleLook::Checkbox);
    CHECK(node(nodes, "sound").spec.on);
    const UiSpec& volume = node(nodes, "volume").spec;
    CHECK(volume.min == 0);
    CHECK(volume.max == 10);
    CHECK(volume.step == 2);
    CHECK(volume.value == 4);
    const UiSpec& hp = node(nodes, "hp").spec;
    CHECK(node(nodes, "hp").kind == Kind::ProgressBar);
    CHECK(hp.direction == FillDirection::Up);
    CHECK(hp.value == 0.25f);
    CHECK(hp.animate == doctest::Approx(0.3f));
    CHECK(node(nodes, "row").spec.layout == LayoutMode::Row);
}

TEST_CASE("Sizes parse in all forms") {
    auto parse = [](const char* value) {
        std::string json = std::string(R"({"type": "panel", "width": )") + value + "}";
        std::vector<NodeSpec> nodes;
        LoadResult r = loadLayout("s.json", [&](std::string_view) { return std::optional<std::string>(json); },
                                  nullptr, nodes);
        REQUIRE(r.ok);
        CHECK(r.errors.empty());
        return nodes[0].spec.width;
    };
    CHECK(parse("120") == Size::px(120));
    CHECK(parse(R"("fill")") == Size::fill(1));
    CHECK(parse(R"("fill:2")") == Size::fill(2));
    CHECK(parse(R"("fit")") == Size::fit());
}

TEST_CASE("Theme file parses styles, states and extends") {
    Theme theme = Theme::builtin();
    LoadResult r = loadTheme("theme.json", readTestFile, theme);
    REQUIRE(r.ok);
    CHECK(r.errors.empty());
    const Style& primary = theme.resolve("primary", VisualState::Normal);
    CHECK(primary.background.value().kind == Background::Kind::Shape);
    CHECK(primary.background.value().radius == 16);
    CHECK(primary.background.value().color == Color::fromHex(0x3B82F6FF));
    CHECK(primary.font.value() == "fonts/Fredoka.ttf");
    CHECK(primary.fontSize.value() == 40);
    CHECK(primary.padding.value() == Insets{8, 16, 8, 16});
    CHECK(primary.clickSound.value() == "sfx/hit.wav");
    CHECK(primary.outlineColor.value() == std::optional<Color>(Color::Black));
    CHECK(theme.resolve("primary", VisualState::Pressed).tint.value() == Color::fromHex(0xD0D0D0FF));
    CHECK(theme.resolve("primary", VisualState::Disabled).opacity.value() == doctest::Approx(0.4f));
    const Style& danger = theme.resolve("danger", VisualState::Normal);
    CHECK(danger.background.value().color == Color::fromHex(0xEF4444FF));
    CHECK(danger.font.value() == "fonts/Fredoka.ttf"); // from primary
    const Style& skin = theme.resolve("skin", VisualState::Normal);
    CHECK(skin.background.value().kind == Background::Kind::Sprite);
    CHECK(skin.background.value().sprite == "sprites/paddle");
    CHECK(skin.background.value().slice == Insets{4, 8, 4, 8});
    CHECK(skin.tint.value() == Color::fromHex(0xFFFFFFCC));
}

TEST_CASE("Unknown keys, wrong types and bad styles are reported with paths") {
    Theme theme = Theme::builtin();
    std::vector<NodeSpec> nodes;
    LoadResult r = loadLayout("bad_keys.json", readTestFile, &theme, nodes);
    CHECK(r.ok); // bad values fall back to defaults; the file still loads
    CHECK(hasError(r, R"(bad_keys.json: children[0] (button "play"): unknown key "heigth")"));
    CHECK(hasError(r, R"(bad_keys.json: children[0] (button "play"): "gap" must be a number)"));
    CHECK(hasError(r, R"(bad_keys.json: children[0] (button "play"): unknown style "nonexistent")"));
    CHECK(hasError(r, R"(bad_keys.json: children[1]: unknown type "spinner")"));
    CHECK(hasError(r, R"(bad_keys.json: duplicate id "play")"));
    CHECK(r.firstError == r.errors.front());
}

TEST_CASE("Syntax errors report line and column") {
    std::vector<NodeSpec> nodes;
    LoadResult r = loadLayout("bad_syntax.json", readTestFile, nullptr, nodes);
    CHECK(!r.ok);
    CHECK(r.firstError.rfind("bad_syntax.json:3:", 0) == 0);
}

TEST_CASE("Include inserts a file with overrides") {
    std::vector<NodeSpec> nodes;
    LoadResult r = loadLayout("includes.json", readTestFile, nullptr, nodes);
    REQUIRE(r.ok);
    CHECK(r.errors.empty());
    const NodeSpec& coins = node(nodes, "coins2");
    CHECK(coins.spec.gap == 9);                 // overridden
    CHECK(coins.spec.layout == LayoutMode::Row); // from the included file
    CHECK(node(nodes, "coin_count").kind == Kind::Label);
    CHECK(r.files == std::vector<std::string>{"includes.json", "parts/coin.json"});
}

TEST_CASE("Loader rejects include cycles and odd shapes") {
    std::vector<NodeSpec> nodes;
    LoadResult cycle = loadLayout("cycle_a.json", readTestFile, nullptr, nodes);
    CHECK(!cycle.ok);
    CHECK(cycle.firstError == "cycle_a.json: include cycle: cycle_a.json -> cycle_b.json -> cycle_a.json");
    LoadResult empty = loadLayout("empty_children.json", readTestFile, nullptr, nodes);
    CHECK(empty.ok);
    CHECK(nodes.size() == 1);
    LoadResult odd = loadLayout("odd.json", readTestFile, nullptr, nodes);
    CHECK(hasError(odd, "odd.json: children[0] must be an object"));
    LoadResult big = loadLayout("big.json", readTestFile, nullptr, nodes);
    CHECK(big.ok);
    CHECK(nodes.size() == 200);
    LoadResult missing = loadLayout("nope.json", readTestFile, nullptr, nodes);
    CHECK(!missing.ok);
    CHECK(missing.firstError == "nope.json: file not found");
}

TEST_CASE("Failed load keeps the current tree") {
    LogCapture logs;
    UiFixture fx;
    uiSetFileReader(fx.canvas, readTestFile);
    fx.canvas.load("menu.json");
    fx.frame();
    CHECK(fx.canvas.button("play").valid());
    CHECK(fx.canvas.label("title").valid());
    fx.canvas.load("bad_syntax.json");
    fx.frame();
    CHECK(fx.canvas.button("play").valid());
    CHECK(logs.count("bad_syntax.json:3:") == 1);
    fx.canvas.load("includes.json"); // a good file replaces the tree
    fx.frame();
    CHECK(!fx.canvas.button("play").valid());
    CHECK(fx.canvas.label("coin_count").valid());
}

namespace {
std::optional<std::string> readTextFields(std::string_view) {
    return std::string(R"({ "type": "panel", "children": [
        { "type": "label", "id": "a", "text": "x", "wrap": true, "maxLines": 2, "overflow": "shrink",
          "minScale": 0.5, "lineSpacing": 1.2, "textAlign": "end" },
        { "type": "label", "id": "b", "text": "y", "textAlign": "start", "maxLines": -1, "overflow": "squash" },
        { "type": "image", "id": "c", "sprite": "s/a", "spriteRtl": "s/b", "mirror": false } ] })");
}
} // namespace

TEST_CASE("Text layout fields parse") {
    std::vector<NodeSpec> nodes;
    LoadResult r = loadLayout("text.json", readTextFields, nullptr, nodes);
    REQUIRE(r.ok);
    const UiSpec& a = node(nodes, "a").spec;
    CHECK(a.wrap);
    CHECK(a.maxLines == 2);
    CHECK(a.overflow == Overflow::Shrink);
    CHECK(a.minScale == doctest::Approx(0.5f));
    CHECK(a.lineSpacing == doctest::Approx(1.2f));
    CHECK(a.textAlign == TextAlignH::End);
    const UiSpec& b = node(nodes, "b").spec;
    CHECK(b.textAlign == TextAlignH::Start);
    CHECK(b.maxLines == 0);
    CHECK(b.overflow == Overflow::Clip);
    CHECK(hasError(r, R"(text.json: children[1] (label "b"): "maxLines" must be a whole number, 0 or more)"));
    CHECK(r.errors.size() == 2); // and the unknown overflow value
    const UiSpec& c = node(nodes, "c").spec;
    CHECK(c.spriteRtl == "s/b");
    CHECK(!c.mirror);
    CHECK(a.mirror);
}

namespace {
std::optional<std::string> readRichTheme(std::string_view) {
    return std::string(R"({ "styles": { "hud": { "font": "fonts/a.ttf", "boldFont": "fonts/a-bold.ttf" } },
                            "colors": { "gold": "#ffcc00", "ink": "#10203040", "bad": "yellow" } })");
}
} // namespace

TEST_CASE("Theme colours and bold fonts parse") {
    Theme theme = Theme::builtin();
    LoadResult r = loadTheme("theme.json", readRichTheme, theme);
    REQUIRE(r.ok);
    CHECK(*theme.resolve("hud", VisualState::Normal).boldFont == "fonts/a-bold.ttf");
    CHECK(theme.color("gold") == Color{0xff, 0xcc, 0x00, 0xff});
    CHECK(theme.color("ink") == Color{0x10, 0x20, 0x30, 0x40});
    CHECK_FALSE(theme.color("bad").has_value());
    CHECK(hasError(r, R"(theme.json: colors: "bad" must be a colour like "#rrggbb" or "#rrggbbaa")"));
}
