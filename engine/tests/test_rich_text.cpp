#include "support/ui_fixture.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/platform/file.hpp>
#include <text/localization_impl.hpp>
#include <text/text_block.hpp>

#include <SDL3_ttf/SDL_ttf.h>

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

// Rich text resolved straight from the fixture's assets (Fredoka, the game's sprite atlas).
struct TestRich final : RichResolver {
    Assets* assets = nullptr;
    FontHandle font(const RunStyle& s, f32 factor) const override {
        return assets->font("fonts/Fredoka.ttf", 32 * s.sizeScale * factor, FontStyle{s.bold, s.italic});
    }
    std::optional<Color> color(const RunStyle& s) const override {
        if (s.color) return s.color;
        if (s.colorName == "gold") return Color{0xff, 0xcc, 0x00, 0xff};
        return std::nullopt;
    }
    SpriteRegion icon(std::string_view name) const override {
        const size_t slash = name.find('/');
        return assets->atlas(name.substr(0, slash)).region(name.substr(slash + 1));
    }
    void markupError(const std::string&) const override {}
};

struct RichFixture : UiFixture {
    FontHandle font32;
    TestRich rich;
    RichFixture() {
        font32 = assets->font("fonts/Fredoka.ttf", 32);
        rich.assets = assets.get();
    }
    TextBlockParams params(std::string_view text) {
        TextBlockParams p;
        p.fonts = std::span<const FontHandle>(&font32, 1);
        p.text = text;
        p.rich = &rich;
        return p;
    }
    f32 fontHeight() { return f32(TTF_GetFontHeight(text->ttfFont(font32))) / text->pixelScale(); }
};

struct LabelFixture : UiFixture {
    LabelFixture() {
        text->lang().impl().setReader(readTestText);
        REQUIRE(text->lang().setLanguage("en"));
        Style def = shared->theme.resolve("default", VisualState::Normal);
        def.font = std::string("fonts/Fredoka.ttf");
        shared->theme.set("default", def);
    }
    Element& el(const ElementRef& r) { return *canvas.impl().store.get(r.handle()); }
};

} // namespace

TEST_CASE("Rich text wraps across styled runs") {
    RichFixture fx;
    TextBlock block(*fx.text);
    TextBlockParams p = fx.params("[b]Buy[/b] [icon=sprites/ball] for [color=gold]250[/color] coins");
    p.wrapWidth = 160;
    REQUIRE(block.set(p));
    REQUIRE(block.rich());
    CHECK(block.shownText() == "Buy  for 250 coins");
    CHECK(block.lineCount() >= 2);
    CHECK(block.size().x <= 160.5f);
    const auto icons = block.icons(160, TextAlignH::Left);
    REQUIRE(icons.size() == 1);
    CHECK(icons[0].region.texture.valid());
    CHECK(icons[0].box.h == doctest::Approx(fx.fontHeight()).epsilon(0.03));
    // The icon sits on the first line's baseline: its bottom is where the line's tallest piece
    // (the icon itself) ends, i.e. the icon starts at the line's top.
    CHECK(std::abs(icons[0].box.y) <= 1);
    CHECK(icons[0].box.x > 0);

    // Unchanged text: no re-layout, no allocation.
    const u32 rebuilds = fx.text->rebuildsTotal();
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    for (int i = 0; i < 400; ++i) {
        block.set(p);
        (void)block.icons(160, TextAlignH::Left);
    }
    CHECK(memory::allocationCount() - before == 0);
    CHECK(fx.text->rebuildsTotal() == rebuilds);
}

TEST_CASE("Rich text in right-to-left lines runs right to left") {
    RichFixture fx;
    TextBlock block(*fx.text);
    TextBlockParams p = fx.params("[icon=sprites/ball] [b]10[/b]");
    REQUIRE(block.set(p));
    const f32 w = block.size().x;
    auto icons = block.icons(w, TextAlignH::Left);
    REQUIRE(icons.size() == 1);
    CHECK(icons[0].box.x == doctest::Approx(0));
    p.rtl = true;
    REQUIRE(block.set(p));
    icons = block.icons(w, TextAlignH::Left);
    REQUIRE(icons.size() == 1);
    CHECK(icons[0].box.x + icons[0].box.w == doctest::Approx(w).epsilon(0.01)); // first run on the right
}

TEST_CASE("Rich text truncates with an ellipsis and shrinks") {
    RichFixture fx;
    TextBlock block(*fx.text);
    TextBlockParams p = fx.params("[b]One[/b] two three four five six seven eight nine ten");
    p.wrapWidth = 200;
    p.maxLines = 2;
    REQUIRE(block.set(p));
    CHECK(block.lineCount() == 2);
    CHECK(block.size().x <= 200.5f);
    const std::string_view shown = block.shownText();
    CHECK(shown.substr(shown.size() - 3) == "…");

    std::vector<FontHandle> sizes;
    for (f32 s = 32; s >= 32 * 0.6f; s -= 2) sizes.push_back(fx.assets->font("fonts/Fredoka.ttf", s));
    TextBlockParams q = fx.params("[b]Settings[/b] and more");
    q.fonts = sizes;
    q.fitBox = {220, 60};
    REQUIRE(block.set(q));
    CHECK(block.fontIndex() > 0);
    CHECK(block.size().x <= 220.5f);
}

TEST_CASE("Placeholder values never inject tags or placeholders") {
    LabelFixture fx;
    auto plain = fx.canvas.root().add<Label>({.text = "x"});
    auto rich = fx.canvas.root().add<Label>({.text = "x", .offset = {0, 100}});
    plain.setKey("player.hello", {{"name", "[color=red]x{score}"}, {"score", i64(5)}});
    rich.setKey("player.rich", {{"name", "[color=red]x{score}"}, {"score", i64(5)}});
    fx.frame();
    CHECK(fx.el(plain).rendered == "Hi [[color=red]x{score}");
    REQUIRE(fx.el(plain).text);
    CHECK(fx.el(plain).text->shownText() == "Hi [color=red]x{score}");
    REQUIRE(fx.el(rich).text);
    CHECK(fx.el(rich).text->shownText() == "Hi [color=red]x{score}");
    // The bold run holds the whole value: no colour tag was opened inside it.
    REQUIRE(fx.el(rich).text->pieceCount() >= 2);

    char buf[128];
    const size_t n = fx.text->lang().format(buf, "player.rich", {{"name", "[b]"}});
    CHECK(std::string_view(buf, n) == "Hi [b][[b][/b]");
}

TEST_CASE("Label switches between empty, plain and rich text cleanly") {
    LabelFixture fx;
    const u32 before = fx.text->liveTextCount();
    auto label = fx.canvas.root().add<Label>({.text = ""});
    fx.frame();
    label.setText("plain");
    fx.frame();
    const u32 plainCount = fx.text->liveTextCount();
    CHECK(plainCount == before + 1);
    label.setText("[b]rich[/b] [icon=sprites/ball] text");
    fx.frame();
    CHECK(fx.el(label).text->rich());
    CHECK(fx.text->liveTextCount() > plainCount);
    CHECK(fx.el(label).text->icons(400, TextAlignH::Left).size() == 1);
    label.setText("plain");
    fx.frame();
    CHECK_FALSE(fx.el(label).text->rich());
    CHECK(fx.text->liveTextCount() == plainCount);
    CHECK(fx.el(label).text->icons(400, TextAlignH::Left).empty());
    label.setText("");
    fx.frame();
    CHECK(fx.el(label).text->empty());
}

TEST_CASE("Bold uses the style's boldFont") {
    LabelFixture fx;
    setAssetRoot(NOVA_TEST_DATA_DIR);
    Style withBold;
    withBold.font = std::string("fonts/NotoSansHebrew-sub.ttf");
    withBold.boldFont = std::string("fonts/NotoSansArabic-sub.ttf");
    fx.shared->theme.set("withBold", withBold);
    Style synthetic;
    synthetic.font = std::string("fonts/NotoSansHebrew-sub.ttf");
    fx.shared->theme.set("synthetic", synthetic);
    auto a = fx.canvas.root().add<Label>({.style = "withBold", .text = "[b]Bold[/b] plain"});
    auto b = fx.canvas.root().add<Label>({.style = "synthetic", .text = "[b]Bold[/b] plain", .offset = {0, 100}});
    fx.frame();
    TextBlock& ta = *fx.el(a).text;
    TextBlock& tb = *fx.el(b).text;
    REQUIRE(ta.pieceCount() == 3); // "Bold", " ", "plain"
    REQUIRE(tb.pieceCount() == 3);
    CHECK(ta.pieceFont(2) == fx.el(a).font);
    CHECK_FALSE(ta.pieceFont(0) == fx.el(a).font);
    CHECK((TTF_GetFontStyle(fx.text->ttfFont(ta.pieceFont(0))) & TTF_STYLE_BOLD) == 0); // a real bold file
    CHECK_FALSE(tb.pieceFont(0) == fx.el(b).font);
    CHECK((TTF_GetFontStyle(fx.text->ttfFont(tb.pieceFont(0))) & TTF_STYLE_BOLD) != 0); // synthetic bold
    setAssetRoot(NOVA_TEST_ASSET_ROOT);
}

TEST_CASE("Theme colours name rich text colours") {
    LabelFixture fx;
    fx.shared->theme.setColor("gold", Color{0xff, 0xcc, 0x00, 0xff});
    CHECK(fx.shared->theme.color("gold") == Color{0xff, 0xcc, 0x00, 0xff});
    CHECK_FALSE(fx.shared->theme.color("nope").has_value());
}

TEST_CASE("Rich runs draw in their colour") {
    RichFixture fx;
    TextBlock block(*fx.text);
    REQUIRE(block.set(fx.params("[color=#ff0000]RED[/color] WHITE")));
    const ScreenFit& fit = fx.renderer->fit();
    fx.renderer->beginFrame();
    block.draw({fit.visible.x + 20, fit.visible.y + 20}, 600, TextAlignH::Left, fit, Color::White, 1);
    SDL_Surface* shot = SDL_RenderReadPixels(fx.renderer->sdl(), nullptr);
    REQUIRE(shot);
    SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(shot);
    REQUIRE(rgba);
    int red = 0, white = 0;
    for (int y = 0; y < rgba->h; ++y) {
        const u8* row = static_cast<const u8*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch);
        for (int x = 0; x < rgba->w; ++x) {
            const u8* px = row + x * 4;
            if (px[0] > 200 && px[1] < 60 && px[2] < 60) ++red;
            if (px[0] > 200 && px[1] > 200 && px[2] > 200) ++white;
        }
    }
    SDL_DestroySurface(rgba);
    fx.renderer->endFrame();
    CHECK(red > 20);
    CHECK(white > 20);
}
