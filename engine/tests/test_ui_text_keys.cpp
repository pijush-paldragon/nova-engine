#include "support/ui_fixture.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/ecs/world.hpp>
#include <text/localization_impl.hpp>
#include <ui/reload.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace nova;
using namespace nova::ui;
using nova::testing::UiFixture;
namespace fs = std::filesystem;

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

struct TextFixture : UiFixture {
    TextFixture() {
        text->lang().impl().setReader(readTestText);
        REQUIRE(text->lang().setLanguage("en"));
        Style def = shared->theme.resolve("default", VisualState::Normal);
        def.font = std::string("fonts/Fredoka.ttf");
        shared->theme.set("default", def);
    }
    std::string shown(const ElementRef& e) { return canvas.impl().store.get(e.handle())->rendered; }
};

} // namespace

TEST_CASE("@ keys in layout files are translated") {
    TextFixture fx;
    auto play = fx.canvas.root().add<Label>({.text = "@menu.play"});
    fx.frame();
    CHECK(fx.shown(play) == "Play");
    REQUIRE(fx.text->lang().setLanguage("ja"));
    fx.frame();
    CHECK(fx.shown(play) == "プレイ");
}

TEST_CASE("@@ escapes a literal at sign") {
    TextFixture fx;
    auto mail = fx.canvas.root().add<Label>({.text = "@@home"});
    fx.frame();
    CHECK(fx.shown(mail) == "@home");
}

TEST_CASE("setKey re-renders with arguments") {
    TextFixture fx;
    auto score = fx.canvas.root().add<Label>({.id = "score"});
    score.setKey("hud.score", {{"score", i64(5)}});
    fx.frame();
    CHECK(fx.shown(score) == "Score 5");
    for (int i = 10; i < 20; ++i) {
        score.setKey("hud.score", {{"score", i64(i)}});
        fx.frame();
    }
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    for (int i = 20; i < 99; ++i) {
        score.setKey("hud.score", {{"score", i64(i)}});
        fx.frame();
    }
    const u64 used = memory::allocationCount() - before;
    CHECK(used == 0);
    CHECK(fx.shown(score) == "Score 98");
}

TEST_CASE("World TextLabel keys translate too") {
    TextFixture fx;
    World world;
    Entity e = world.spawn();
    world.add<Transform>(e, Transform{.pos{100, 100}});
    auto& label = world.add<TextLabel>(e, TextLabel{.font = fx.assets->font("fonts/Fredoka.ttf", 32)});
    label.setKey("hud.score", {{"score", i64(7)}});
    fx.text->collectAndDraw(world, fx.renderer->fit(), {0, 0});
    CHECK(label.displayed() == "Score 7");
    REQUIRE(fx.text->lang().setLanguage("ja"));
    label.setKey("menu.play");
    fx.text->collectAndDraw(world, fx.renderer->fit(), {0, 0});
    CHECK(label.displayed() == "プレイ");
}

TEST_CASE("Language switch updates every scene's labels") {
    TextFixture fx;
    Canvas below; // a paused scene's canvas, drawn but not updated
    uiAttach(below, &fx.services, kGlobalScope);
    auto top = fx.canvas.root().add<Label>({.text = "@menu.play"});
    auto under = below.root().add<Label>({.text = "@menu.play"});
    PopupRef popup = fx.canvas.push(UiSpec{.width = Size::px(200), .height = Size::px(100)});
    auto inPopup = popup.root().add<Label>({.text = "@menu.play"});
    World world;
    Entity e = world.spawn();
    world.add<Transform>(e);
    auto& label = world.add<TextLabel>(e, TextLabel{.font = fx.assets->font("fonts/Fredoka.ttf", 32)});
    label.setKey("menu.play");
    fx.frame();
    uiDraw(below, *fx.renderer, fx.batch);
    fx.text->collectAndDraw(world, fx.renderer->fit(), {0, 0});

    REQUIRE(fx.text->lang().setLanguage("ja"));
    fx.frame();
    uiDraw(below, *fx.renderer, fx.batch);
    fx.text->collectAndDraw(world, fx.renderer->fit(), {0, 0});
    CHECK(fx.shown(top) == "プレイ");
    CHECK(fx.shown(inPopup) == "プレイ");
    CHECK(below.impl().store.get(under.handle())->rendered == "プレイ");
    CHECK(label.displayed() == "プレイ");
    uiReset(below);
}

TEST_CASE("Editing the language file reloads it") {
    const fs::path dir = fs::temp_directory_path() / ("nova_lang_reload_" + std::to_string(SDL_GetTicksNS()));
    fs::create_directories(dir / "text");
    auto write = [&](const char* play, int bump) {
        {
            std::ofstream out(dir / "text/en.json");
            out << R"({ "language": "en", "strings": { "menu": { "play": ")" << play << R"(" } } })";
        }
        fs::last_write_time(dir / "text/en.json", fs::file_time_type::clock::now() + std::chrono::seconds(2 * bump));
    };
    write("Play", 1);
    detail::setDevAssetSourceDir((dir.string() + "/").c_str());
    {
        TextFixture fx;
        fx.text->lang().impl().setReader(sourceFileReader());
        REQUIRE(fx.text->lang().setLanguage("en"));
        auto play = fx.canvas.root().add<Label>({.text = "@menu.play"});
        fx.frame();
        CHECK(fx.shown(play) == "Play");
        write("Go!", 2);
        for (int i = 0; i < 4; ++i) fx.frame(0.2f);
        CHECK(fx.shown(play) == "Go!");
    }
    detail::setDevAssetSourceDir("");
    fs::remove_all(dir);
}

TEST_CASE("Keyed plurals pick their form from count") {
    TextFixture fx;
    char buf[64];
    const FormatArg one[] = {{"count", i64(1)}};
    const FormatArg three[] = {{"count", i64(3)}};
    CHECK(std::string_view(buf, fx.text->lang().formatArgs(buf, "hud.lives", one)) == "1 life");
    CHECK(std::string_view(buf, fx.text->lang().formatArgs(buf, "hud.lives", three)) == "3 lives");
    auto lives = fx.canvas.root().add<Label>({.text = "x"});
    lives.setKey("hud.lives", {{"count", i64(1)}});
    fx.frame();
    CHECK(fx.shown(lives) == "1 life");
    lives.setKey("hud.lives", {{"count", i64(2)}});
    fx.frame();
    CHECK(fx.shown(lives) == "2 lives");
}
