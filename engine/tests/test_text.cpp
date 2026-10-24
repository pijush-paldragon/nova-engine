#include <doctest/doctest.h>
#include <nova/core/alloc_stats.hpp>
#include <nova/ecs/world.hpp>
#include <nova/text/text.hpp>
#include <text/text_backend.hpp>

#include <SDL3/SDL.h>

using namespace nova;

namespace {

struct SoftwareRenderer {
    SDL_Surface* surface = SDL_CreateSurface(720, 1280, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    ~SoftwareRenderer() {
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }
};

const ScreenFit kFit = computeFit({720, 1280}, {720, 1280});

} // namespace

TEST_CASE("Text measures and rebuilds only on change") {
    SoftwareRenderer sw;
    REQUIRE(sw.renderer);
    auto created = TextBackend::create(sw.renderer);
    REQUIRE(created.ok());
    TextBackend& text = *created.value();
    auto font = text.loadFont("fonts/Fredoka.ttf@48");
    REQUIRE(font.has_value());
    {
        World w;
        auto e = w.spawn();
        w.add<Transform>(e, Transform{.pos{360, 100}});
        auto& label = w.add<TextLabel>(e, TextLabel{.font = *font, .align = TextAlign::Center});
        label.set("Score 0");
        text.collectAndDraw(w, kFit, {0, 0});
        CHECK(text.rebuildsTotal() == 1);
        CHECK(text.liveTextCount() == 1);
        u32 rev = w.get<TextLabel>(e).revision();
        w.get<TextLabel>(e).set("Score 0");
        CHECK(w.get<TextLabel>(e).revision() == rev);
        text.collectAndDraw(w, kFit, {0, 0});
        CHECK(text.rebuildsTotal() == 1);
        w.get<TextLabel>(e).set("Score 10");
        text.collectAndDraw(w, kFit, {0, 0});
        CHECK(text.rebuildsTotal() == 2);
        CHECK(text.measure(*font, "Score 10").x > 0);
        CHECK(text.measure(*font, "Score 10").x > text.measure(*font, "S").x);
    }
    CHECK(text.liveTextCount() == 0); // World teardown released the TTF_Text
    text.unloadFont(*font);
}

TEST_CASE("Text missing font is invalid and drawing it is harmless") {
    SoftwareRenderer sw;
    auto created = TextBackend::create(sw.renderer);
    REQUIRE(created.ok());
    TextBackend& text = *created.value();
    CHECK(!text.loadFont("fonts/nope.ttf@32").has_value());
    CHECK(!text.loadFont("fonts/Fredoka.ttf").has_value()); // key without @size
    World w;
    auto e = w.spawn();
    w.add<Transform>(e);
    w.add<TextLabel>(e).set("hello");
    text.collectAndDraw(w, kFit, {0, 0});
    CHECK(text.liveTextCount() == 0);
    CHECK(text.measure(FontHandle{}, "x") == Vec2{0, 0});
}

TEST_CASE("Text label destroy releases TTF_Text and pixel-scale change rebuilds") {
    SoftwareRenderer sw;
    auto created = TextBackend::create(sw.renderer);
    REQUIRE(created.ok());
    TextBackend& text = *created.value();
    auto font = text.loadFont("fonts/Fredoka.ttf@32");
    REQUIRE(font.has_value());
    World w;
    auto e = w.spawn();
    w.add<Transform>(e);
    w.add<TextLabel>(e, TextLabel{.font = *font}).set("hi");
    text.collectAndDraw(w, kFit, {0, 0});
    CHECK(text.rebuildsTotal() == 1);
    text.setPixelScale(2.f);
    text.collectAndDraw(w, kFit, {0, 0});
    CHECK(text.rebuildsTotal() == 2);
    w.destroy(e);
    w.flushDestroyed();
    CHECK(text.liveTextCount() == 0);
    text.unloadFont(*font);
}

#if defined(NOVA_TRACK_ALLOCS)
TEST_CASE("Text adding labels after warm-up does not allocate in engine code") {
    SoftwareRenderer sw;
    auto created = TextBackend::create(sw.renderer);
    REQUIRE(created.ok());
    TextBackend& text = *created.value();
    auto font = text.loadFont("fonts/Fredoka.ttf@32");
    REQUIRE(font.has_value());
    {
        World w;
        w.reserve<Transform, TextLabel>(256);
        auto first = w.spawn();
        w.add<Transform>(first);
        w.add<TextLabel>(first, TextLabel{.font = *font}).set("0123456789 Score");
        text.collectAndDraw(w, kFit, {0, 0}); // warm-up: glyphs, first TextCache
        memory::markGameThread();
        auto before = memory::allocationCount();
        for (int i = 0; i < 100; ++i) { // e.g. floating "+10" labels spawned during play
            auto e = w.spawn();
            w.add<Transform>(e, Transform{.pos{float(i), 50}});
            w.add<TextLabel>(e, TextLabel{.font = *font}).set("+10");
        }
        text.collectAndDraw(w, kFit, {0, 0});
        CHECK(memory::allocationCount() == before);
    }
    text.unloadFont(*font);
}
#endif

#if defined(NOVA_TRACK_ALLOCS)
TEST_CASE("Text adding labels in a right-to-left language does not allocate in engine code") {
    SoftwareRenderer sw;
    auto created = TextBackend::create(sw.renderer);
    REQUIRE(created.ok());
    TextBackend& text = *created.value();
    REQUIRE(text.lang().setLanguage("ar")); // Brick Breaker's text/ar.json
    auto font = text.loadFont("fonts/Fredoka.ttf@32");
    REQUIRE(font.has_value());
    {
        World w;
        w.reserve<Transform, TextLabel>(256);
        auto first = w.spawn();
        w.add<Transform>(first);
        w.add<TextLabel>(first, TextLabel{.font = *font}).set("0123456789 Score");
        text.collectAndDraw(w, kFit, {0, 0});
        memory::markGameThread();
        auto before = memory::allocationCount();
        for (int i = 0; i < 100; ++i) {
            auto e = w.spawn();
            w.add<Transform>(e, Transform{.pos{float(i), 50}});
            w.add<TextLabel>(e, TextLabel{.font = *font}).set("+10");
        }
        text.collectAndDraw(w, kFit, {0, 0});
        CHECK(memory::allocationCount() == before);
    }
    text.unloadFont(*font);
}
#endif
