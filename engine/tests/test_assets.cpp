#include <doctest/doctest.h>
#include <nova/assets/asset_cache.hpp>
#include <nova/assets/atlas.hpp>
#include <nova/platform/file.hpp>

using namespace nova;

TEST_CASE("AssetCache dedups, scopes, and purges") {
    int loads = 0, unloads = 0;
    struct T;
    AssetCache<int, T> cache{
        [&](std::string_view p) -> std::optional<int> {
            ++loads;
            return p == "missing" ? std::nullopt : std::optional<int>{1};
        },
        [&](int&) { ++unloads; }};
    auto a = cache.acquire("a.png", 1), a2 = cache.acquire("a.png", 2);
    CHECK(a == a2);
    CHECK(loads == 1);
    cache.releaseScope(1);
    cache.purgeUnreferenced();
    CHECK(unloads == 0); // scope 2 still holds it
    cache.releaseScope(2);
    CHECK(cache.get(a) != nullptr); // cached until purge
    cache.purgeUnreferenced();
    CHECK(unloads == 1);
    CHECK(cache.get(a) == nullptr);
    CHECK(!cache.acquire("missing", 1).valid());
    CHECK(!cache.acquire("missing", 1).valid());
    CHECK(loads == 2); // failure cached, not retried
    auto g = cache.acquire("g.png", kGlobalScope);
    cache.releaseScope(1);
    cache.purgeUnreferenced();
    CHECK(cache.get(g));
    CHECK(cache.loadedCount() == 1);
    cache.clear();
    CHECK(unloads == 2);
    CHECK(cache.loadedCount() == 0);
}

TEST_CASE("AssetCache reacquire after purge reloads with a fresh handle") {
    int loads = 0;
    struct T;
    AssetCache<int, T> cache{[&](std::string_view) -> std::optional<int> { return ++loads; },
                             [](int&) {}};
    auto a = cache.acquire("a.png", 1);
    cache.releaseScope(1);
    cache.purgeUnreferenced();
    auto b = cache.acquire("a.png", 1);
    CHECK(b.valid());
    CHECK(!(a == b));
    CHECK(*cache.get(b) == 2);
}

TEST_CASE("parseAtlas reads regions and rejects malformed lines") {
    auto r = parseAtlas("image sprites.png\n# c\npaddle 2 2 160 32\n\nball 164 2 32 32\n");
    REQUIRE(r.ok());
    CHECK(r.value().size() == 2);
    CHECK(r.value()[1].name == "ball");
    CHECK(r.value()[1].src.w == 32);
    CHECK(!parseAtlas("image s.png\npaddle 2 2 x 32\n").ok());
    CHECK(!parseAtlas("paddle 2 2 1 32\n").ok()); // missing image line
    CHECK(parseAtlasImage("image sprites.png\n").value() == "sprites.png");
}

TEST_CASE("readTextFile loads generated atlas from the asset root") {
    auto text = readTextFile("sprites.atlas");
    REQUIRE(text.ok());
    auto regions = parseAtlas(text.value());
    REQUIRE(regions.ok());
    CHECK(regions.value().size() == 5);
    CHECK(!readTextFile("does/not/exist.txt").ok());
}
