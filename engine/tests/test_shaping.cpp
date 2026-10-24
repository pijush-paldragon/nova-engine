// Text shaping through SDL_ttf with HarfBuzz (subset OFL Noto fonts in engine/tests/data/fonts).
#include <doctest/doctest.h>

#include <SDL3_ttf/SDL_ttf.h>

#include <string>

namespace {

std::string fontPath(const char* name) { return std::string(NOVA_TEST_DATA_DIR) + "fonts/" + name; }

struct Ttf {
    Ttf() { REQUIRE(TTF_Init()); }
    ~Ttf() { TTF_Quit(); }
};

struct Font {
    TTF_Font* f;
    explicit Font(const std::string& path, float size = 32) : f(TTF_OpenFont(path.c_str(), size)) { REQUIRE(f); }
    ~Font() { TTF_CloseFont(f); }
};

int width(TTF_Font* f, const char* utf8) {
    int w = 0, h = 0;
    REQUIRE(TTF_GetStringSize(f, utf8, 0, &w, &h));
    return w;
}

// Width of each codepoint measured alone, summed (no shaping across letters).
int isolatedWidth(TTF_Font* f, const std::string& s) {
    int total = 0;
    for (size_t i = 0; i < s.size();) {
        size_t n = 1;
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0xF0) n = 4;
        else if (c >= 0xE0) n = 3;
        else if (c >= 0xC0) n = 2;
        total += width(f, s.substr(i, n).c_str());
        i += n;
    }
    return total;
}

} // namespace

TEST_CASE("Arabic letters join when shaped") {
    Ttf ttf;
    Font arabic(fontPath("NotoSansArabic-sub.ttf"));
    const std::string word = "سلام";
    // Joined (medial/final) forms are narrower than each letter's isolated form.
    CHECK(width(arabic.f, word.c_str()) < isolatedWidth(arabic.f, word));
}

TEST_CASE("Devanagari forms clusters") {
    Ttf ttf;
    Font deva(fontPath("NotoSansDevanagari-sub.ttf"));
    // "कि" = ka + vowel sign i. The sign comes last in memory but is drawn before (left of) the
    // consonant; only a shaper (HarfBuzz) reorders it.
    const std::string syllable = "कि";
    TTF_Text* text = TTF_CreateText(nullptr, deva.f, syllable.c_str(), 0);
    REQUIRE(text);
    TTF_SubString ka{}, sign{};
    REQUIRE(TTF_GetTextSubString(text, 0, &ka));
    REQUIRE(TTF_GetTextSubString(text, 3, &sign)); // the sign's UTF-8 offset
    CHECK(sign.rect.x <= ka.rect.x);
    CHECK(sign.cluster_index == ka.cluster_index); // one cluster
    TTF_DestroyText(text);
}

TEST_CASE("Fallback font supplies missing glyphs") {
    Ttf ttf;
    const std::string fredoka = std::string(NOVA_TEST_ASSET_ROOT) + "fonts/Fredoka.ttf";
    Font game(fredoka);
    Font jp(fontPath("NotoSansJP-sub.ttf"));
    CHECK(!TTF_FontHasGlyph(game.f, 0x65E5)); // 日
    REQUIRE(TTF_AddFallbackFont(game.f, jp.f));
    const int withFallback = width(game.f, "日本語");
    CHECK(std::abs(withFallback - width(jp.f, "日本語")) <= 1);
    TTF_ClearFallbackFonts(game.f);
}

// ---- Language fonts (TextBackend + Assets) ------------------------------------------------------

#include "support/ui_fixture.hpp"

#include <nova/platform/file.hpp>
#include <text/localization_impl.hpp>

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

// Assets read engine/tests/data/ (its fonts/ folder holds the Noto subsets).
struct LangFixture : nova::testing::UiFixture {
    LangFixture() {
        nova::setAssetRoot(NOVA_TEST_DATA_DIR);
        text->lang().impl().setReader(readTestText);
        assets->setLocalization(&text->lang());
    }
    ~LangFixture() { nova::setAssetRoot(NOVA_TEST_ASSET_ROOT); }
};

} // namespace

TEST_CASE("Language fonts attach as fallbacks") {
    LangFixture fx;
    REQUIRE(fx.text->lang().setLanguage("ja"));
    nova::FontHandle ja = fx.assets->font("fonts/NotoSansHebrew-sub.ttf", 32); // no Japanese glyphs itself
    REQUIRE(ja.valid());
    Font jp(fontPath("NotoSansJP-sub.ttf"), 32 * fx.renderer->pixelScale());
    const nova::Vec2 shaped = fx.text->measure(ja, "日本語");
    CHECK(std::abs(shaped.x * fx.renderer->pixelScale() - width(jp.f, "日本語")) <= 2);

    REQUIRE(fx.text->lang().setLanguage("en"));
    nova::FontHandle en = fx.assets->font("fonts/NotoSansHebrew-sub.ttf", 32);
    CHECK(!(en == ja)); // a different font object for a different language
}

TEST_CASE("Right-to-left language lays text right to left") {
    LangFixture fx;
    REQUIRE(fx.text->lang().setLanguage("ar"));
    nova::FontHandle f = fx.assets->font("fonts/NotoSansHebrew-sub.ttf", 32);
    TTF_Font* font = fx.text->ttfFont(f);
    REQUIRE(font);
    auto firstAndLastX = [&](const char* s, int lastOffset) {
        TTF_Text* t = TTF_CreateText(nullptr, font, s, 0);
        REQUIRE(t);
        TTF_SubString a{}, b{};
        REQUIRE(TTF_GetTextSubString(t, 0, &a));
        REQUIRE(TTF_GetTextSubString(t, lastOffset, &b));
        TTF_DestroyText(t);
        return std::pair<int, int>{a.rect.x, b.rect.x};
    };
    auto [arFirst, arLast] = firstAndLastX("سلام", 6); // Arabic (via the fallback): first letter on the right
    CHECK(arFirst > arLast);
    auto [enFirst, enLast] = firstAndLastX("abc", 2);  // Latin in the Arabic UI stays left to right
    CHECK(enFirst < enLast);
}
