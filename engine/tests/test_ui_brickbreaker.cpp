// Brick Breaker's UI files (the engine tests read Brick Breaker's assets).
#include "support/ui_fixture.hpp"

#include <nova/core/alloc_stats.hpp>
#include <ui/loader.hpp>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace nova;
using namespace nova::ui;
using nova::testing::UiFixture;

namespace {

struct Expect {
    const char* file;
    std::vector<std::pair<const char*, Kind>> ids; // every id the game's scenes bind
};

const std::vector<Expect> kScreens = {
    {"ui/menu.json", {{"best", Kind::Label}, {"play", Kind::Button}, {"settings", Kind::Button}}},
    {"ui/hud.json", {{"score", Kind::Label}, {"level", Kind::Label}, {"lives", Kind::Label}, {"pause", Kind::Button}}},
    {"ui/pause.json", {{"resume", Kind::Button}, {"settings", Kind::Button}, {"quit", Kind::Button}}},
    {"ui/settings.json", {{"music", Kind::Slider}, {"sfx", Kind::Slider}, {"sound", Kind::Toggle}, {"close", Kind::Button}}},
    {"ui/game_over.json", {{"score", Kind::Label}, {"best", Kind::Label}, {"again", Kind::Button}, {"menu", Kind::Button}}},
};

} // namespace

TEST_CASE("Every Brick Breaker layout loads without errors") {
    Theme theme = Theme::builtin();
    LoadResult themeResult = loadTheme("ui/theme.json", assetFileReader(), theme);
    REQUIRE(themeResult.ok);
    CHECK(themeResult.errors.empty());
    for (const Expect& screen : kScreens) {
        CAPTURE(screen.file);
        std::vector<NodeSpec> nodes;
        LoadResult r = loadLayout(screen.file, assetFileReader(), &theme, nodes);
        REQUIRE(r.ok);
        CHECK(r.errors.empty());
        if (!r.errors.empty()) MESSAGE(r.errors.front());
        for (auto [id, kind] : screen.ids) {
            CAPTURE(id);
            bool found = false;
            for (const NodeSpec& n : nodes) found |= n.spec.id == id && n.kind == kind;
            CHECK(found);
        }
    }
}

TEST_CASE("Menu draws within the draw-call budget") {
    UiFixture fx; // no world sprites: every draw call is the UI's
    loadGameTheme(*fx.shared, assetFileReader());
    fx.canvas.load("ui/menu.json");
    fx.frame();
    fx.frame();
    CHECK(fx.canvas.button("play").valid());
    CHECK(fx.renderer->drawCallsLastFrame() <= 3);
}

TEST_CASE("HUD score updates allocate nothing") {
    UiFixture fx;
    loadGameTheme(*fx.shared, assetFileReader());
    fx.canvas.load("ui/hud.json");
    REQUIRE(fx.canvas.label("score").valid());
    char text[32];
    auto run = [&] {
        for (int i = 1; i <= 400; ++i) {
            std::snprintf(text, sizeof text, "Score %d", i);
            fx.canvas.label("score").setText(text);
            fx.frame();
        }
    };
    run(); // warm-up: every length the score reaches
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    run();
    CHECK(memory::allocationCount() == before);
}
