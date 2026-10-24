#include "support/ui_fixture.hpp"

#include <ui/loader.hpp>
#include <ui/reload.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace nova;
using namespace nova::ui;
using nova::testing::UiFixture;
namespace fs = std::filesystem;

namespace {

// A temporary source-assets folder; files written here bump their mtime explicitly so the
// change is seen even within the filesystem's timestamp resolution.
struct SourceDir {
    fs::path dir = fs::temp_directory_path() / ("nova_ui_reload_" + std::to_string(SDL_GetTicksNS()));
    int bumps = 0;

    SourceDir() {
        fs::create_directories(dir / "ui" / "parts");
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

std::string menu(const char* title, const char* okOffset = "[0, 200]") {
    return std::string(R"({ "type": "panel", "width": "fill", "height": "fill", "children": [
        { "type": "label", "id": "title", "text": ")") + title + R"(" },
        { "type": "label", "id": "score", "text": "0" },
        { "type": "slider", "id": "volume", "width": 400, "offset": [0, 400] },
        { "type": "button", "id": "ok", "width": 200, "height": 100, "offset": )" + okOffset + R"( } ] })";
}

void settle(UiFixture& fx) {
    for (int i = 0; i < 4; ++i) fx.frame(0.2f);
}

} // namespace

TEST_CASE("Editing a layout file rebuilds the screen") {
    SourceDir src;
    UiFixture fx;
    uiSetFileReader(fx.canvas, src.reader());
    src.write("ui/menu.json", menu("One"));
    fx.canvas.load("ui/menu.json");
    fx.frame();
    CHECK(fx.canvas.label("title").valid());
    src.write("ui/menu.json", menu("Two"));
    fx.frame(0.6f);
    fx.frame();
    CHECK(std::string(fx.canvas.impl().store.get(fx.canvas.label("title").handle())->spec.text) == "Two");
}

TEST_CASE("Reload keeps bindings, API values and player values") {
    SourceDir src;
    UiFixture fx;
    uiSetFileReader(fx.canvas, src.reader());
    src.write("ui/menu.json", menu("One"));
    fx.canvas.load("ui/menu.json");
    int clicks = 0;
    fx.canvas.onClick("ok", [&] { ++clicks; });
    fx.canvas.label("score").setText("42");
    fx.frame();
    fx.press(1, {300, 424}); // slider at x 0..400, y 400
    fx.frame();
    fx.release(1, {300, 424});
    fx.frame();
    CHECK(fx.canvas.slider("volume").value() == doctest::Approx(0.75f));

    src.write("ui/menu.json", menu("One", "[0, 700]")); // the button moved
    settle(fx);
    fx.tap({100, 750});
    CHECK(clicks == 1);
    CHECK(fx.canvas.impl().store.get(fx.canvas.label("score").handle())->spec.text == "42");
    CHECK(fx.canvas.slider("volume").value() == doctest::Approx(0.75f));
}

TEST_CASE("A broken edit keeps the old UI and shows the error") {
    SourceDir src;
    UiFixture fx;
    uiSetFileReader(fx.canvas, src.reader());
    src.write("ui/menu.json", menu("One"));
    fx.canvas.load("ui/menu.json");
    settle(fx);
    src.write("ui/menu.json", "{ \"type\": \"panel\", ");
    settle(fx);
    CHECK(fx.canvas.button("ok").valid());
    CHECK(std::string(fx.canvas.devError()).rfind("ui/menu.json:", 0) == 0);
    src.write("ui/menu.json", menu("Fixed"));
    settle(fx);
    CHECK(fx.canvas.devError().empty());
    CHECK(fx.canvas.impl().store.get(fx.canvas.label("title").handle())->spec.text == "Fixed");

    src.write("ui/menu.json", R"({ "type": "panel", "heigth": 3 })"); // validation errors count too
    settle(fx);
    CHECK(fx.canvas.button("ok").valid());
    CHECK(!fx.canvas.devError().empty());
}

TEST_CASE("Editing an included file or the theme reloads") {
    SourceDir src;
    UiFixture fx;
    uiSetFileReader(fx.canvas, src.reader());
    src.write("ui/parts/coin.json", R"({ "type": "label", "id": "coins", "text": "1" })");
    src.write("ui/hud.json", R"({ "type": "panel", "children": [ { "include": "ui/parts/coin.json" } ] })");
    src.write("ui/theme.json", R"({ "styles": { "gold": { "textColor": "#ffd166" } } })");
    loadGameTheme(*fx.shared, src.reader());
    fx.canvas.load("ui/hud.json");
    settle(fx);
    src.write("ui/parts/coin.json", R"({ "type": "label", "id": "coins", "text": "2" })");
    settle(fx);
    CHECK(fx.canvas.impl().store.get(fx.canvas.label("coins").handle())->spec.text == "2");

    const u32 gen = fx.shared->theme.generation();
    src.write("ui/theme.json", R"({ "styles": { "gold": { "textColor": "#ff0000" } } })");
    settle(fx);
    CHECK(fx.shared->theme.generation() != gen);
    CHECK(fx.shared->theme.resolve("gold", VisualState::Normal).textColor.value() == Color::fromHex(0xFF0000FF));
}

TEST_CASE("Reload from inside a click callback") {
    SourceDir src;
    UiFixture fx;
    uiSetFileReader(fx.canvas, src.reader());
    src.write("ui/menu.json", menu("One"));
    fx.canvas.load("ui/menu.json");
    fx.canvas.onClick("ok", [&] {
        src.write("ui/menu.json", menu("Again"));
        fx.canvas.load("ui/menu.json");
    });
    fx.frame();
    fx.tap({100, 250});
    fx.frame();
    CHECK(fx.canvas.impl().store.get(fx.canvas.label("title").handle())->spec.text == "Again");
}

TEST_CASE("No polling without a source dir") {
    SourceDir src;
    UiFixture fx;
    uiSetFileReader(fx.canvas, src.reader());
    src.write("ui/menu.json", menu("One"));
    fx.canvas.load("ui/menu.json");
    fx.frame();
    detail::setDevAssetSourceDir("");
    src.write("ui/menu.json", menu("Two"));
    for (int i = 0; i < 12; ++i) fx.frame(0.2f);
    CHECK(fx.canvas.impl().store.get(fx.canvas.label("title").handle())->spec.text == "One");
}
