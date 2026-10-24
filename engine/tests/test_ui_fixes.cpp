// Regression tests for the whole-branch review's findings (UI core).
#include "support/ui_fixture.hpp"

#include <nova/core/alloc_stats.hpp>
#include <ui/layout.hpp>
#include <ui/loader.hpp>
#include <ui/reload.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace nova;
using namespace nova::ui;
using nova::testing::UiFixture;
namespace fs = std::filesystem;

namespace {
Style withFont(Style s) {
    s.font = "fonts/Fredoka.ttf";
    s.fontSize = 32;
    return s;
}
void settle(UiFixture& fx, int n = 20, f32 dt = 0.05f) {
    for (int i = 0; i < n; ++i) fx.frame(dt);
}
struct SourceDir {
    fs::path dir = fs::temp_directory_path() / ("nova_ui_review_" + std::to_string(SDL_GetTicksNS()));
    int bumps = 0;
    SourceDir() {
        fs::create_directories(dir / "ui");
        detail::setDevAssetSourceDir((dir.string() + "/").c_str());
    }
    ~SourceDir() {
        detail::setDevAssetSourceDir("");
        fs::remove_all(dir);
    }
    void write(const std::string& rel, const std::string& text) {
        {
            std::ofstream out(dir / rel, std::ios::binary);
            out << text;
        }
        fs::last_write_time(dir / rel, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
    }
    FileReader reader() const {
        return [d = dir](std::string_view rel) -> std::optional<std::string> {
            std::ifstream in(d / std::string(rel), std::ios::binary);
            if (!in) return std::nullopt;
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
    }
};
} // namespace

TEST_CASE("Review: popup root offset survives") {
    UiFixture fx;
    PopupRef p = fx.canvas.push(UiSpec{.width = Size::px(200), .height = Size::px(200), .offset = {100, 100}},
                                {.enter = Transition::None});
    settle(fx);
    Rect r = visualBox(fx.canvas.impl().store, p.root().handle());
    MESSAGE("popup visual box x=" << r.x << " y=" << r.y);
    CHECK(r.x == doctest::Approx(100));
    CHECK(r.y == doctest::Approx(100));
}

TEST_CASE("Review: unknown style allocates every frame") {
    UiFixture fx;
    fx.canvas.root().add<Panel>({.style = "nope", .width = Size::px(100), .height = Size::px(100)});
    settle(fx, 5);
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    settle(fx, 10);
    const u64 used = memory::allocationCount() - before; // before MESSAGE, which allocates
    MESSAGE("allocations in 10 frames: " << used);
    CHECK(used == 0);
}

TEST_CASE("Review: clearing a label and setting it again allocates") {
    UiFixture fx;
    fx.shared->theme.set("default", withFont(fx.shared->theme.resolve("default", VisualState::Normal)));
    auto l = fx.canvas.root().add<Label>({.text = "Combo"});
    for (int i = 0; i < 3; ++i) {
        l.setText("");
        fx.frame();
        l.setText("Combo");
        fx.frame();
    }
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    l.setText("");
    fx.frame();
    l.setText("Combo");
    fx.frame();
    const u64 used = memory::allocationCount() - before;
    MESSAGE("allocations for one clear/set cycle: " << used);
    CHECK(used == 0);
}

TEST_CASE("Review: frames after a file popup push allocate") {
    UiFixture fx;
    loadGameTheme(*fx.shared, assetFileReader());
    fx.canvas.load("ui/hud.json");
    settle(fx, 5);
    memory::markGameThread();
    PopupRef p = fx.canvas.push("ui/pause.json");
    REQUIRE(p.valid());
    const u64 before = memory::allocationCount();
    fx.frame();
    const u64 first = memory::allocationCount() - before;
    settle(fx, 5);
    const u64 used = memory::allocationCount() - before;
    MESSAGE("allocations in the frame after push: " << first << ", total 6 frames: " << used);
    CHECK(used == 0);
}

TEST_CASE("Review: code popup push allocates outside LoadingScope") {
    UiFixture fx;
    fx.canvas.root().add<Button>({.id = "b", .width = Size::px(100), .height = Size::px(100)});
    settle(fx, 5);
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    PopupRef p = fx.canvas.push(UiSpec{.width = Size::px(200), .height = Size::px(200)}, {.closeOnBackdrop = true});
    const u64 push = memory::allocationCount() - before;
    settle(fx, 3);
    const u64 used = memory::allocationCount() - before;
    MESSAGE("allocations in push(UiSpec): " << push << ", plus 3 frames: " << used);
    CHECK(used == 0);
}

TEST_CASE("Review: extends cannot set a value back to the Style default") {
    Theme t = Theme::builtin();
    Style big;
    big.fontSize = 84;
    big.textColor = Color::fromHex(0xFFD166FF);
    big.padding = Insets::all(20);
    t.set("big", big);
    Style small;
    small.extends = "big";
    small.fontSize = 32;
    small.textColor = Color::White;
    small.padding = Insets::all(0);
    t.set("small", small);
    const Style& s = t.resolve("small", VisualState::Normal);
    MESSAGE("small: fontSize " << s.fontSize.value() << " textColor.g " << int(s.textColor.value().g) << " padding " << s.padding.value().top);
    CHECK(s.fontSize.value() == 32);
    CHECK(s.textColor.value() == Color::White);
    CHECK(s.padding.value().top == 0);
}

TEST_CASE("Review: same id in two popups shares one binding") {
    UiFixture fx;
    PopupRef a = fx.canvas.push(UiSpec{.width = Size::px(400), .height = Size::px(400)}, {.enter = Transition::None});
    a.root().add<Button>({.id = "close", .width = Size::px(200), .height = Size::px(100)});
    int aClicks = 0, bClicks = 0;
    a.button("close").onClick([&] { ++aClicks; });
    PopupRef b = fx.canvas.push(UiSpec{.width = Size::px(400), .height = Size::px(400)}, {.enter = Transition::None, .exit = Transition::None});
    b.root().add<Button>({.id = "close", .width = Size::px(200), .height = Size::px(100)});
    b.button("close").onClick([&] { ++bClicks; });
    settle(fx);
    b.close();
    settle(fx);
    REQUIRE(fx.canvas.popupCount() == 1);
    fx.tap({160 + 100, 440 + 50});
    MESSAGE("aClicks=" << aClicks << " bClicks=" << bClicks);
    CHECK(aClicks == 1);
    CHECK(bClicks == 0);
}

TEST_CASE("Review: PopupRef after live reload of its file") {
    SourceDir src;
    UiFixture fx;
    uiSetFileReader(fx.canvas, src.reader());
    src.write("ui/dlg.json", R"({ "type": "panel", "width": 400, "height": 400, "children": [
        { "type": "button", "id": "close", "width": 200, "height": 100 } ] })");
    PopupRef p = fx.canvas.push("ui/dlg.json", {.enter = Transition::None, .exit = Transition::None});
    REQUIRE(p.valid());
    settle(fx, 4, 0.2f);
    src.write("ui/dlg.json", R"({ "type": "panel", "width": 400, "height": 400, "children": [
        { "type": "button", "id": "close", "width": 220, "height": 100 } ] })");
    settle(fx, 4, 0.2f);
    MESSAGE("devError: " << std::string(fx.canvas.devError()));
    CHECK(p.valid());
    p.close();
    settle(fx, 4, 0.2f);
    CHECK(fx.canvas.popupCount() == 0);
}

TEST_CASE("Review: slider keeps changing after a popup cancels its touch") {
    UiFixture fx;
    auto s = fx.canvas.root().add<Slider>({.id = "vol", .width = Size::px(400), .height = Size::px(48), .offset = {100, 100}});
    int changes = 0;
    s.onChange([&](f32) { ++changes; });
    settle(fx, 2);
    fx.press(1, {150, 124});
    fx.frame();
    const int before = changes;
    const f32 v0 = s.value();
    fx.canvas.push(UiSpec{.width = Size::px(200), .height = Size::px(200)});
    fx.move(1, {450, 124});
    fx.frame();
    MESSAGE("value " << v0 << " -> " << s.value() << ", changes after popup " << (changes - before));
    CHECK(s.value() == v0);
    CHECK(changes == before);
}

TEST_CASE("Review: finger held on gameplay stays visible to gameplay under a popup") {
    UiFixture fx;
    settle(fx, 2);
    fx.press(1, {600, 1200});
    fx.frame();
    fx.canvas.push(UiSpec{.width = Size::px(200), .height = Size::px(200)});
    settle(fx, 5);
    fx.move(1, {500, 1200});
    fx.beginUi();
    MESSAGE("primary under popup: " << (fx.input.primary() != nullptr));
    CHECK(fx.input.primary() == nullptr);
    fx.endFrame();
}

TEST_CASE("Review: sprite background re-resolves (and allocates) every frame") {
    UiFixture fx;
    Style skin;
    skin.background->kind = Background::Kind::Sprite;
    skin.background->sprite = "ui/skins/button_blue_rounded_large.png";
    fx.shared->theme.set("skin", skin);
    fx.canvas.root().add<Panel>({.style = "skin", .width = Size::px(100), .height = Size::px(100)});
    settle(fx, 5);
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    settle(fx, 10);
    const u64 used = memory::allocationCount() - before; // before MESSAGE, which allocates
    MESSAGE("allocations in 10 frames: " << used);
    CHECK(used == 0);
}

TEST_CASE("Review: fit-height safeArea panel (HUD shape) ignores the inset when measuring") {
    Canvas c;
    auto hud = c.root().add<Panel>({.width = Size::fill(), .layout = LayoutMode::Row, .padding = Insets{12, 24, 0, 24},
                                    .align = Align::Center, .safeArea = true});
    auto btn = hud.add<Panel>({.width = Size::px(96), .height = Size::px(96)});
    LayoutEnv env{{0, 0, 720, 1560}, {0, 110, 720, 1400}, 1}; // notch: safe area starts at y 110
    layoutCanvas(c.impl().store, env, nullptr, nullptr);
    MESSAGE("hud box y=" << hud.box().y << " h=" << hud.box().h << "; button y=" << btn.box().y);
    CHECK(btn.box().y == 110 + 12);
    CHECK(hud.box().h == 110 + 12 + 96); // the fit height grew by the inset
}


TEST_CASE("Review: layout-file transitions are used") {
    UiFixture fx;
    std::string screen = R"({ "type": "panel", "width": 200, "height": 200, "enter": "fade" })";
    std::string dialog = R"({ "type": "panel", "width": 200, "height": 200, "enter": "none" })";
    uiSetFileReader(fx.canvas, [&](std::string_view p) {
        return std::optional<std::string>(p == "screen.json" ? screen : dialog);
    });
    fx.canvas.load("screen.json");
    fx.frame(0.05f);
    const UiHandle root = fx.canvas.impl().store.find("");
    (void)root;
    UiHandle content;
    fx.canvas.impl().store.forEachChild(fx.canvas.impl().store.layerRoot(0), [&](UiHandle h, const Element& e) {
        if (!e.removed) content = h;
    });
    const f32 midway = fx.canvas.impl().store.get(content)->spec.opacity;
    CHECK(midway > 0.f);
    CHECK(midway < 1.f); // fading in
    settle(fx);
    CHECK(fx.canvas.impl().store.get(content)->spec.opacity == doctest::Approx(1.f));

    PopupRef p = fx.canvas.push("dialog.json"); // caller left enter unset: the file's "none" applies
    fx.frame(0);
    CHECK(fx.canvas.impl().layerInfo[1].phase == LayerInfo::Phase::Open);
    PopupRef q = fx.canvas.push("dialog.json", {.enter = Transition::Pop}); // the caller's choice wins
    fx.frame(0);
    CHECK(fx.canvas.impl().layerInfo[2].phase == LayerInfo::Phase::Entering);
}

TEST_CASE("Review: theme.json patches existing styles instead of replacing them") {
    Theme t = Theme::builtin();
    LoadResult r = loadTheme("t.json", [](std::string_view) {
        return std::optional<std::string>(R"({ "styles": {
            "default": { "font": "fonts/Fredoka.ttf" },
            "big": { "fontSize": 84, "textColor": "#ffd166", "padding": 20 },
            "small": { "extends": "big", "fontSize": 32, "textColor": "#ffffff", "padding": 0,
                       "background": "none" } } })");
    }, t);
    REQUIRE(r.ok);
    CHECK(r.errors.empty());
    const Style& def = t.resolve("default", VisualState::Pressed);
    CHECK(def.font.value() == "fonts/Fredoka.ttf");
    CHECK(def.scale.value() == doctest::Approx(0.95f)); // built-in pressed look kept
    CHECK(t.resolve("primary", VisualState::Disabled).opacity.value() == doctest::Approx(0.5f));
    const Style& small = t.resolve("small", VisualState::Normal);
    CHECK(small.fontSize.value() == 32);
    CHECK(small.textColor.value() == Color::White);
    CHECK(small.padding->top == 0);
}

TEST_CASE("Review: disabled buttons block the touch from gameplay") {
    UiFixture fx;
    auto play = fx.canvas.root().add<Button>({.id = "play", .width = Size::px(200), .height = Size::px(100)});
    int clicks = 0;
    fx.canvas.onClick("play", [&] { ++clicks; });
    play.setEnabled(false);
    fx.frame();
    fx.press(1, {100, 50});
    fx.release(1, {100, 50});
    fx.beginUi();
    CHECK(!fx.input.tapped());
    CHECK(fx.input.pointer(0)->consumedByUi);
    fx.endFrame();
    fx.frame();
    CHECK(clicks == 0);
}
