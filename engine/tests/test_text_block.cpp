#include "support/ui_fixture.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/platform/file.hpp>
#include <text/localization_impl.hpp>
#include <text/text_block.hpp>
#include <ui/layout.hpp>

#include <SDL3_ttf/SDL_ttf.h>

#include <string>
#include <vector>

using namespace nova;
using namespace nova::ui;
using nova::testing::UiFixture;

namespace {

const char* kSentence = "The quick brown fox jumps over the lazy dog again and again";

struct BlockFixture : UiFixture {
    FontHandle font32;
    BlockFixture() { font32 = assets->font("fonts/Fredoka.ttf", 32); }
    TextBlockParams params(std::string_view text) {
        TextBlockParams p;
        p.fonts = std::span<const FontHandle>(&font32, 1);
        p.text = text;
        return p;
    }
};

std::optional<std::string> readTestData(std::string_view path) {
    std::string full = std::string(NOVA_TEST_DATA_DIR) + std::string(path);
    size_t size = 0;
    void* data = SDL_LoadFile(full.c_str(), &size);
    if (!data) return std::nullopt;
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    return text;
}

} // namespace

TEST_CASE("Wrap breaks at the width") {
    BlockFixture fx;
    TextBlock block(*fx.text);
    TextBlockParams p = fx.params(kSentence);
    p.wrapWidth = 200;
    REQUIRE(block.set(p));
    CHECK(block.lineCount() >= 3);
    CHECK(block.size().x <= 200.5f);
}

TEST_CASE("Fit height grows with wrapped lines") {
    UiFixture fx;
    Style def = fx.shared->theme.resolve("default", VisualState::Normal);
    def.font = std::string("fonts/Fredoka.ttf");
    fx.shared->theme.set("default", def);
    auto one = fx.canvas.root().add<Label>({.text = "Short", .width = Size::px(200), .wrap = true});
    auto many = fx.canvas.root().add<Label>({.text = kSentence, .width = Size::px(200), .offset = {0, 300}, .wrap = true});
    fx.frame();
    const Element& m = *fx.canvas.impl().store.get(many.handle());
    REQUIRE(m.text);
    CHECK(m.text->lineCount() >= 3);
    CHECK(many.box().h == doctest::Approx(m.text->size().y).epsilon(0.02));
    CHECK(many.box().h > 2.5f * one.box().h);
}

TEST_CASE("maxLines truncates with an ellipsis") {
    BlockFixture fx;
    TextBlock block(*fx.text);
    TextBlockParams p = fx.params(kSentence);
    p.wrapWidth = 200;
    p.maxLines = 2;
    REQUIRE(block.set(p));
    CHECK(block.lineCount() == 2);
    const std::string shown(block.shownText());
    CHECK(shown.size() >= 3);
    CHECK(shown.substr(shown.size() - 3) == "…");
}

TEST_CASE("Shrink picks the largest size that fits") {
    BlockFixture fx;
    std::vector<FontHandle> sizes;
    for (f32 s = 32; s >= 32 * 0.6f; s -= 2) sizes.push_back(fx.assets->font("fonts/Fredoka.ttf", s));
    TextBlock block(*fx.text);
    TextBlockParams p = fx.params("Settings and more");
    p.fonts = sizes;
    p.fitBox = {220, 40};
    REQUIRE(block.set(p));
    CHECK(block.fontIndex() > 0);
    CHECK(block.size().x <= 220.5f);
    // A size that fits from the start stays at index 0.
    p.fitBox = {2000, 400};
    REQUIRE(block.set(p));
    CHECK(block.fontIndex() == 0);
}

TEST_CASE("Shrink stops at minScale") {
    BlockFixture fx;
    std::vector<FontHandle> sizes;
    for (f32 s = 32; s >= 32 * 0.6f; s -= 2) sizes.push_back(fx.assets->font("fonts/Fredoka.ttf", s));
    const u32 fontsBefore = fx.text->liveFontCount();
    TextBlock block(*fx.text);
    TextBlockParams p = fx.params(kSentence);
    p.fonts = sizes;
    p.fitBox = {60, 20};
    p.wrapWidth = 60;
    p.maxLines = 1;
    REQUIRE(block.set(p));
    CHECK(block.fontIndex() == sizes.size() - 1);
    CHECK(block.lineCount() == 1);
    CHECK(fx.text->liveFontCount() == fontsBefore); // no extra font loads
}

TEST_CASE("Start and End follow the language direction") {
    CHECK(resolveAlign(TextAlignH::Start, false) == TextAlignH::Left);
    CHECK(resolveAlign(TextAlignH::End, false) == TextAlignH::Right);
    CHECK(resolveAlign(TextAlignH::Start, true) == TextAlignH::Right);
    CHECK(resolveAlign(TextAlignH::End, true) == TextAlignH::Left);
    CHECK(resolveAlign(TextAlignH::Center, true) == TextAlignH::Center);
}

TEST_CASE("CJK wraps between characters") {
    UiFixture fx;
    setAssetRoot(NOVA_TEST_DATA_DIR);
    fx.text->lang().impl().setReader(readTestData);
    fx.assets->setLocalization(&fx.text->lang());
    REQUIRE(fx.text->lang().setLanguage("ja"));
    FontHandle font = fx.assets->font("fonts/NotoSansHebrew-sub.ttf", 32); // + Japanese fallback
    TextBlock block(*fx.text);
    TextBlockParams p;
    p.fonts = std::span<const FontHandle>(&font, 1);
    p.text = "日本語のゲームを開始します。「設定」を開いてください。";
    p.wrapWidth = 150;
    REQUIRE(block.set(p));
    CHECK(block.lineCount() >= 3);
    for (u32 line = 0; line < block.lineCount(); ++line) {
        const std::string_view text = block.lineText(line);
        CAPTURE(line);
        CHECK(text.rfind("。", 0) != 0);
        CHECK(text.rfind("」", 0) != 0);
        CHECK(text.rfind("、", 0) != 0);
    }
    setAssetRoot(NOVA_TEST_ASSET_ROOT);
}

TEST_CASE("Unchanged text never re-lays out or allocates") {
    BlockFixture fx;
    TextBlock block(*fx.text);
    TextBlockParams p = fx.params(kSentence);
    p.wrapWidth = 200;
    REQUIRE(block.set(p));
    const u32 rebuilds = fx.text->rebuildsTotal();
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    for (int i = 0; i < 400; ++i) block.set(p);
    const u64 used = memory::allocationCount() - before;
    CHECK(used == 0);
    CHECK(fx.text->rebuildsTotal() == rebuilds);
}

TEST_CASE("Wrapped lines centre and space within the box") {
    BlockFixture fx;
    TextBlock block(*fx.text);
    TextBlockParams p = fx.params("The quick brown fox jumps over the lazy dog");
    p.wrapWidth = 300;
    REQUIRE(block.set(p));
    REQUIRE(block.lineCount() >= 2);
    const f32 singleSpaced = block.size().y;

    const ScreenFit& fit = fx.renderer->fit();
    const Vec2 topLeft{fit.visible.x + 60, fit.visible.y + 100};
    const f32 boxWidth = 600;
    fx.renderer->beginFrame();
    block.draw(topLeft, boxWidth, TextAlignH::Center, fit, Color::White, 1);
    SDL_Surface* shot = SDL_RenderReadPixels(fx.renderer->sdl(), nullptr);
    REQUIRE(shot);
    SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(shot);
    REQUIRE(rgba);
    const f32 s = fit.scale;
    const int boxL = int(60 * s), boxR = int((60 + boxWidth) * s);
    const f32 band = block.size().y * s / f32(block.lineCount());
    for (u32 line = 0; line < block.lineCount(); ++line) {
        int lo = rgba->w, hi = -1;
        const int y0 = int(100 * s + band * f32(line)), y1 = int(100 * s + band * f32(line + 1));
        for (int y = y0; y < y1 && y < rgba->h; ++y) {
            const u8* row = static_cast<const u8*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch);
            for (int x = 0; x < rgba->w; ++x) {
                if (row[x * 4] > 128) {
                    lo = std::min(lo, x);
                    hi = std::max(hi, x);
                }
            }
        }
        CAPTURE(line);
        REQUIRE(hi >= lo);
        CHECK(std::abs((lo - boxL) - (boxR - hi)) <= 4); // equal margins: centred
    }
    SDL_DestroySurface(rgba);
    fx.renderer->endFrame();

    p.lineSpacing = 1.5f;
    REQUIRE(block.set(p));
    CHECK(block.size().y > singleSpaced * 1.25f);
}
