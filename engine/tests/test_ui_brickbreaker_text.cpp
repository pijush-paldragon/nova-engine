// Brick Breaker's screens in every language it ships (games/brickbreaker/assets/text).

#include "support/ui_fixture.hpp"

#include <text/localization_impl.hpp>
#include <text/text_block.hpp>
#include <ui/loader.hpp>

#include <string>
#include <vector>

using namespace nova;
using namespace nova::ui;
using nova::testing::LogCapture;
using nova::testing::UiFixture;

namespace {

constexpr const char* kLanguages[] = {"en", "de", "ja", "ar"};
constexpr const char* kScreens[] = {"ui/menu.json", "ui/hud.json", "ui/game_over.json"};
constexpr const char* kPopups[] = {"ui/pause.json", "ui/settings.json"};

struct GameFixture : UiFixture {
    GameFixture() {
        assets->setLocalization(&text->lang());
        loadGameTheme(*shared, assetFileReader());
    }
    void use(const char* lang) {
        REQUIRE(text->lang().setLanguage(lang));
        frame();
    }
    // Realistic values for the screens' keyed labels, as the scenes set them.
    void fillValues(std::string_view screen) {
        if (screen == "ui/menu.json") canvas.label("best").setKey("menu.best", {{"score", i64(12345)}});
        if (screen == "ui/hud.json") {
            canvas.label("score").setKey("hud.score", {{"score", i64(99999)}});
            canvas.label("level").setKey("hud.level", {{"level", i64(10)}});
            canvas.label("lives").setKey("hud.lives", {{"count", i64(3)}});
        }
        if (screen == "ui/game_over.json") {
            canvas.label("score").setKey("gameover.score", {{"score", i64(99999)}, {"level", i64(10)}});
            canvas.label("best").setKey("gameover.best", {{"best", i64(99999)}});
        }
    }
    // `settle`: frames to run before each check, so enter/exit transitions have finished.
    template <class F>
    void forEachScreen(F&& check, int settle = 1) {
        auto frames = [&](int n) {
            for (int i = 0; i < n; ++i) frame();
        };
        for (const char* screen : kScreens) {
            canvas.load(screen);
            fillValues(screen);
            frames(settle);
            check(screen);
        }
        for (const char* popup : kPopups) {
            canvas.load("ui/menu.json");
            fillValues("ui/menu.json");
            PopupRef p = canvas.push(popup, {.enter = Transition::None});
            frames(settle);
            check(popup);
            p.close();
            frames(settle);
        }
    }
};

} // namespace

TEST_CASE("Brick Breaker strings are complete") {
    GameFixture fx;
    LogCapture logs;
    for (const char* lang : kLanguages) {
        fx.use(lang);
        fx.forEachScreen([&](const char* screenName) {
            const std::string screen = screenName, language = lang;
            fx.canvas.impl().store.forEach([&](UiHandle, Element& e) {
                if (e.removed || e.spec.text.size() < 2 || e.spec.text[0] != '@') return;
                CAPTURE(language);
                CAPTURE(screen);
                CAPTURE(e.spec.text);
                CHECK(e.rendered != "[" + e.spec.text.substr(1) + "]");
                CHECK(e.rendered.find('{') == std::string::npos); // every placeholder filled
            });
        });
    }
    CHECK(logs.count("missing key") == 0);
    CHECK(logs.count("markup:") == 0);
    CHECK(logs.count("failed to load") == 0); // every language's fallback fonts ship with the game
}

TEST_CASE("Every screen fits in every language without overflowing") {
    GameFixture fx;
    for (const char* lang : kLanguages) {
        fx.use(lang);
        fx.forEachScreen([&](const char* screenName) {
            const std::string screen = screenName, language = lang;
            fx.canvas.impl().store.forEach([&](UiHandle, Element& e) {
                if (e.removed || !e.text || e.text->empty() || !e.spec.visible) return;
                const f32 inner = e.box.w - e.padding.left - e.padding.right;
                CAPTURE(language);
                CAPTURE(screen);
                CAPTURE(e.rendered);
                CHECK(e.text->size().x <= inner + 1);
                CHECK(e.text->fontIndex() < std::max<size_t>(e.fonts.size(), 1));
            });
        });
    }
}

TEST_CASE("Arabic mirrors the HUD") {
    GameFixture fx;
    fx.use("ar");
    fx.canvas.load("ui/hud.json");
    fx.fillValues("ui/hud.json");
    fx.frame();
    const Rect score = fx.canvas.label("score").box();
    const Rect lives = fx.canvas.label("lives").box();
    const Rect pause = fx.canvas.button("pause").box();
    CHECK(score.x > lives.x);  // score on the right
    CHECK(pause.x < lives.x);  // pause button on the left
    fx.use("en");
    CHECK(fx.canvas.label("score").box().x < fx.canvas.label("lives").box().x);
    CHECK(fx.canvas.button("pause").box().x > fx.canvas.label("lives").box().x);
}

// Not a check: with NOVA_SHOTS_DIR set, saves every screen in every language as PNGs to look at
// (software renderer, 360x640 window).
TEST_CASE("Brick Breaker screens render in every language") {
    const char* dir = SDL_getenv("NOVA_SHOTS_DIR");
    if (!dir) return;
    GameFixture fx;
    for (const char* lang : kLanguages) {
        fx.use(lang);
        fx.forEachScreen([&](const char* screen) {
            std::string name = std::string(screen).substr(3); // drop "ui/"
            name = name.substr(0, name.size() - 5);           // drop ".json"
            fx.renderer->requestScreenshot(std::string(dir) + "/" + name + "-" + lang + ".png");
            fx.frame();
        }, 30);
    }
}
