#include <doctest/doctest.h>
#include <nova/core/assert.hpp>
#include <nova/core/math.hpp>
#include <nova/core/random.hpp>
#include <nova/core/result.hpp>

using namespace nova;

TEST_CASE("Result carries value or error") {
    Result<int> ok = 5;
    CHECK(ok.ok());
    CHECK(ok.value() == 5);
    auto bad = Result<int>::fail("nope");
    CHECK(!bad);
    CHECK(bad.error().message == "nope");
    CHECK(Result<void>::success().ok());
    CHECK(!Result<void>::fail("x").ok());
}

TEST_CASE("Vec2 math") {
    CHECK(Vec2{3, 4}.length() == doctest::Approx(5));
    CHECK(Vec2{0, 0}.normalized().x == 0); // no NaN
    CHECK(lerp(Vec2{0, 0}, Vec2{10, 20}, 0.5f).y == doctest::Approx(10));
    CHECK((Vec2{1, 2} + Vec2{3, 4}).x == doctest::Approx(4));
    CHECK((Vec2{1, 2} * 2.f).y == doctest::Approx(4));
    CHECK(dot(Vec2{1, 0}, Vec2{0, 1}) == doctest::Approx(0));
}

TEST_CASE("Rect contains/intersects") {
    Rect r{0, 0, 10, 10};
    CHECK(r.contains({5, 5}));
    CHECK(!r.contains({11, 5}));
    CHECK(r.intersects({9, 9, 5, 5}));
    CHECK(!r.intersects({10.5f, 0, 2, 2}));
    CHECK(r.center().x == doctest::Approx(5));
}

TEST_CASE("Affine2 rotates 90 degrees about origin then translates") {
    auto m = Affine2::trs({100, 0}, kPi / 2, {1, 1});
    auto p = m.apply({1, 0});
    CHECK(p.x == doctest::Approx(100));
    CHECK(p.y == doctest::Approx(1));
    auto s = Affine2::trs({0, 0}, 0, {2, 3}).apply({1, 1});
    CHECK(s.x == doctest::Approx(2));
    CHECK(s.y == doctest::Approx(3));
}

TEST_CASE("Color fromHex and Random determinism") {
    constexpr auto c = Color::fromHex(0xFF8000C0);
    CHECK(c.r == 255);
    CHECK(c.g == 128);
    CHECK(c.b == 0);
    CHECK(c.a == 192);
    CHECK(Color::White.withAlpha(7).a == 7);
    Random a{7}, b{7};
    CHECK(a.next() == b.next());
    for (int i = 0; i < 1000; ++i) {
        f32 v = a.range(-1, 1);
        CHECK(v >= -1);
        CHECK(v < 1);
        i32 n = a.rangeInt(3, 5);
        CHECK(n >= 3);
        CHECK(n <= 5);
    }
}

TEST_CASE("Assert failures are counted so frame-limited runs can fail on them") {
    nova::u32 before = nova::detail::assertFailureCount();
    CHECK(!NOVA_VERIFY(1 + 1 == 3, "deliberate failure"));
    CHECK(NOVA_VERIFY(true, "fine"));
    CHECK(nova::detail::assertFailureCount() == before + 1);
}
