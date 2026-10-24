#include <text/line_break.hpp>
#include <text/markup.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace nova;

TEST_CASE("Markup parses every tag") {
    std::string storage;
    std::vector<MarkupRun> runs;
    std::vector<std::string> errors;
    CHECK_FALSE(parseMarkup("no tags here", storage, runs, &errors));
    CHECK(runs.empty());

    const std::string src = "[b]Buy[/b] for [icon=sprites/coin scale=1.5] [color=#ffcc00]2[i]5[/i][/color]"
                            "[size=0.5][style=hud][color=gold]0[/color][/style][/size] [[x]";
    REQUIRE(parseMarkup(src, storage, runs, &errors));
    CHECK(errors.empty());
    CHECK(storage == "Buy for  250 [x]");
    REQUIRE(runs.size() == 8);
    CHECK(runs[0].text == "Buy");
    CHECK(runs[0].style.bold);
    CHECK(runs[1].text == " for ");
    CHECK(runs[1].style.plain());
    CHECK(runs[2].kind == MarkupRun::Kind::Icon);
    CHECK(runs[2].icon == "sprites/coin");
    CHECK(runs[2].iconScale == doctest::Approx(1.5f));
    CHECK(runs[3].text == " ");
    CHECK(runs[4].text == "2");
    CHECK(runs[4].style.color == Color{0xff, 0xcc, 0x00, 0xff});
    CHECK(runs[5].text == "5");
    CHECK(runs[5].style.italic);
    CHECK(runs[5].style.color == Color{0xff, 0xcc, 0x00, 0xff}); // nested: keeps the outer colour
    CHECK(runs[6].text == "0");
    CHECK(runs[6].style.sizeScale == doctest::Approx(0.5f));
    CHECK(runs[6].style.styleName == "hud");
    CHECK(runs[6].style.colorName == "gold");
    CHECK_FALSE(runs[6].style.color.has_value());
    CHECK(runs[7].text == " [x]"); // [[ is a literal [
    CHECK(runs[7].style.plain());
}

TEST_CASE("Unknown and unclosed tags render as text") {
    std::string storage;
    std::vector<MarkupRun> runs;
    std::vector<std::string> errors;
    REQUIRE(parseMarkup("[wobble]hi[/wobble] [b]bold", storage, runs, &errors));
    CHECK(storage == "[wobble]hi[/wobble] [b]bold");
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].style.plain());
    CHECK(errors.size() == 3); // [wobble], [/wobble], unclosed [b]

    errors.clear();
    REQUIRE(parseMarkup("a[/i]b [color=#zz]c[/color] [icon=nope] d[", storage, runs, &errors));
    CHECK(storage == "a[/i]b [color=#zz]c[/color] [icon=nope] d[");
    CHECK(errors.size() == 5); // stray [/i], bad colour, its stray [/color], bad icon, [ without ]
}

TEST_CASE("Escaped values show literally") {
    char buf[64];
    CHECK(escapeMarkup("[b]x", buf) == "[[b]x");
    std::string storage;
    std::vector<MarkupRun> runs;
    const std::string src = "[b]Hi[/b] " + std::string(escapeMarkup("[color=red]x", buf));
    REQUIRE(parseMarkup(src, storage, runs, nullptr));
    CHECK(storage == "Hi [color=red]x");
    REQUIRE(runs.size() == 2);
    CHECK(runs[1].style.plain());
}

TEST_CASE("Break opportunities follow spaces, CJK and kinsoku") {
    std::vector<u32> b;
    breakOpportunities("Hello big world", b);
    CHECK(b == std::vector<u32>{6, 10});
    breakOpportunities("well-known", b);
    CHECK(b == std::vector<u32>{5});
    breakOpportunities("a\nb", b);
    CHECK(b == std::vector<u32>{2});
    const std::string ja = "日本語。です";   // 3 bytes per character
    breakOpportunities(ja, b);
    CHECK(b == std::vector<u32>{3, 6, 12, 15}); // never before 。
    breakOpportunities("（注）", b);
    CHECK(b.empty()); // never after （, never before ）
    breakOpportunities("ok 日本", b);
    CHECK(b == std::vector<u32>{3, 6});
}
