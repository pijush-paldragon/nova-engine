#include "support/ui_fixture.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/text/localization.hpp>
#include <text/localization_impl.hpp>
#include <text/plural.hpp>

#include <SDL3/SDL_iostream.h>

#include <string>
#include <vector>

using namespace nova;
using nova::testing::LogCapture;

namespace {

std::optional<std::string> readTextData(std::string_view path) {
    std::string full = std::string(NOVA_TEST_DATA_DIR) + std::string(path);
    size_t size = 0;
    void* data = SDL_LoadFile(full.c_str(), &size);
    if (!data) return std::nullopt;
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    return text;
}

// A Localization reading engine/tests/data/text/, in `lang`.
struct Lang {
    Localization lang;
    explicit Lang(const char* tag = "en") {
        lang.impl().setReader(readTextData);
        REQUIRE(lang.setLanguage(tag));
    }
};

std::string fmt(const Localization& l, std::string_view key, std::initializer_list<FormatArg> args = {}) {
    char buf[128];
    l.format(buf, key, args);
    return buf;
}

std::string pl(const Localization& l, std::string_view key, i64 n) {
    char buf[128];
    l.plural(buf, key, n);
    return buf;
}

} // namespace

TEST_CASE("Nested keys flatten and resolve") {
    Lang en;
    CHECK(en.lang.get("menu.play") == "Play");
    CHECK(en.lang.get("hud.score") == "Score {score}");
    CHECK(en.lang.language() == "en");
}

TEST_CASE("Plural rules follow CLDR") {
    using P = PluralCategory;
    CHECK(pluralCategory("en", 1) == P::One);
    CHECK(pluralCategory("en", 0) == P::Other);
    CHECK(pluralCategory("en", 2) == P::Other);
    CHECK(pluralCategory("fr", 0) == P::One);
    CHECK(pluralCategory("fr", 1) == P::One);
    CHECK(pluralCategory("fr", 2) == P::Other);
    for (i64 n : {1, 21, 101}) CHECK(pluralCategory("ru", n) == P::One);
    for (i64 n : {2, 3, 4, 22, 104}) CHECK(pluralCategory("ru", n) == P::Few);
    for (i64 n : {0, 5, 11, 12, 14, 25, 111}) CHECK(pluralCategory("ru", n) == P::Many);
    CHECK(pluralCategory("ar", 0) == P::Zero);
    CHECK(pluralCategory("ar", 1) == P::One);
    CHECK(pluralCategory("ar", 2) == P::Two);
    for (i64 n : {3, 10, 103}) CHECK(pluralCategory("ar", n) == P::Few);
    for (i64 n : {11, 99, 111}) CHECK(pluralCategory("ar", n) == P::Many);
    CHECK(pluralCategory("ar", 100) == P::Other);
    CHECK(pluralCategory("ja", 1) == P::Other);
    CHECK(pluralCategory("pt-BR", 0) == P::One);
    CHECK(pluralCategory("xx", 1) == P::One); // unknown: one/other
}

TEST_CASE("Format writes into the caller's buffer") {
    Lang en;
    CHECK(fmt(en.lang, "hud.score", {{"score", i64(120)}}) == "Score 120");
    CHECK(fmt(en.lang, "hud.braces", {{"score", i64(1)}}) == "{literal} 1");
    CHECK(fmt(en.lang, "hud.score") == "Score {score}"); // missing argument stays literal
    CHECK(fmt(en.lang, "player.hello", {{"name", std::string_view("Ann")}}) == "Hi Ann");
    CHECK(fmt(en.lang, "hud.score", {{"score", 2.5}}) == "Score 2.5");
    char tiny[8];
    const size_t n = en.lang.format(tiny, "hud.score", {{"score", i64(123456)}});
    CHECK(n == 7);
    CHECK(std::string(tiny) == "Score 1"); // truncated, still terminated

    char buf[64];
    for (int i = 0; i < 3; ++i) en.lang.format(buf, "hud.score", {{"score", i64(i)}});
    memory::markGameThread();
    const u64 before = memory::allocationCount();
    for (int i = 0; i < 100; ++i) en.lang.format(buf, "hud.score", {{"score", i64(i)}});
    const u64 used = memory::allocationCount() - before;
    CHECK(used == 0);
}

TEST_CASE("Plural picks the form and fills count") {
    Lang en;
    CHECK(pl(en.lang, "hud.lives", 1) == "1 life");
    CHECK(pl(en.lang, "hud.lives", 3) == "3 lives");
    Lang ru("ru");
    CHECK(pl(ru.lang, "hud.lives", 1) == "1 жизнь");
    CHECK(pl(ru.lang, "hud.lives", 3) == "3 жизни");
    CHECK(pl(ru.lang, "hud.lives", 5) == "5 жизней");
    Lang ar("ar");
    CHECK(pl(ar.lang, "hud.lives", 2) == "روحان");
    Lang ja("ja");
    CHECK(pl(ja.lang, "hud.lives", 1) == "残機 1");
}

TEST_CASE("Missing keys fall back to English then the key") {
    LogCapture logs;
    Lang ja("ja");
    CHECK(ja.lang.get("menu.settings") == "Settings");
    CHECK(ja.lang.get("nope.key") == "[nope.key]");
    ja.lang.get("nope.key");
    CHECK(logs.count("text: missing key \"nope.key\"") == 1);
}

TEST_CASE("Language choice: saved, preferred, English") {
    const std::vector<std::string> available{"en", "ja", "pt"};
    CHECK(chooseLanguage(available, "", std::vector<std::string>{"pt-BR", "en"}) == "pt");
    CHECK(chooseLanguage(available, "ja", std::vector<std::string>{"pt-BR"}) == "ja");
    CHECK(chooseLanguage(available, "", std::vector<std::string>{"de"}) == "en");
    CHECK(chooseLanguage(available, "xx", std::vector<std::string>{}) == "en");
    Lang en;
    CHECK(en.lang.available().size() == 7); // from text/languages.json
}

TEST_CASE("Switching language bumps the generation") {
    Lang en;
    const u32 g = en.lang.generation();
    REQUIRE(en.lang.setLanguage("ar"));
    CHECK(en.lang.generation() != g);
    CHECK(en.lang.rightToLeft());
    CHECK(en.lang.get("menu.play") == "العب");
    REQUIRE(en.lang.setLanguage("ja"));
    CHECK(!en.lang.rightToLeft());
    CHECK(en.lang.impl().info().fonts == std::vector<std::string>{"fonts/NotoSansJP-sub.ttf"});
    CHECK(!en.lang.setLanguage("zz")); // no file: stays on ja
    CHECK(en.lang.language() == "ja");
}

TEST_CASE("Bad language file entries fall back per key") {
    LogCapture logs;
    Lang broken("broken");
    CHECK(broken.lang.get("menu.settings") == "Ajustes");
    CHECK(broken.lang.get("menu.play") == "Play");        // 42 is not text: English
    CHECK(pl(broken.lang, "hud.lives", 2) == "2 lives");   // plural without "other": English
    CHECK(broken.lang.get("player.hello") == "Hi {name}"); // array instead of object: English
    CHECK(logs.count("text/broken.json") >= 3);
}
