#include <core/parse.hpp>
#include <doctest/doctest.h>

using namespace nova;

TEST_CASE("parseFloat accepts whole numbers only and is locale-independent") {
    f64 v = 0;
    CHECK(parseFloat("1.5", v));
    CHECK(v == 1.5);
    CHECK(parseFloat("-0.25", v));
    CHECK(v == -0.25);
    CHECK(parseFloat("1e3", v));
    CHECK(v == 1000);
    CHECK(parseFloat("0.30000000000000004", v));
    CHECK(v == 0.30000000000000004);
    CHECK(!parseFloat("", v));
    CHECK(!parseFloat("abc", v));
    CHECK(!parseFloat("1.5x", v));
    CHECK(!parseFloat(" 1.5", v));
    f32 f = 0;
    CHECK(parseFloat("48", f));
    CHECK(f == 48.f);
}
