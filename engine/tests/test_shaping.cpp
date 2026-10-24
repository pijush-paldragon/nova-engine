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
