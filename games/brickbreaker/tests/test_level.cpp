#include "level.hpp"

#include <doctest/doctest.h>

TEST_CASE("makeLevel grows rows and caps at 10") {
    CHECK(makeLevel(1).rows == 5);
    CHECK(makeLevel(6).rows == 10);
    CHECK(makeLevel(50).rows == 10);
    auto l = makeLevel(3);
    CHECK(l.hp.size() == size_t(l.cols * l.rows));
    for (int hp : l.hp) {
        CHECK(hp >= 1);
        CHECK(hp <= 3);
    }
    CHECK(l.hp.front() == 3); // top row hardest
    CHECK(l.hp.back() == 1);  // bottom row weakest
}

TEST_CASE("brick layout fits inside design width") {
    auto l = makeLevel(6);
    for (int row = 0; row < l.rows; ++row) {
        for (int col = 0; col < l.cols; ++col) {
            nova::Rect r = brickRect(col, row, kBricksTop);
            CHECK(r.x >= 0);
            CHECK(r.right() <= 720);
            CHECK(r.w == doctest::Approx(80));
        }
    }
    CHECK(brickRect(0, 0, kBricksTop).x == doctest::Approx(12)); // centred: (720 - 696) / 2
    CHECK(brickRect(1, 1, 300).y == doctest::Approx(300 + 44));
}

TEST_CASE("brickColor maps hit points") {
    CHECK(brickColor(1) == nova::Color::fromHex(0x4CC9F0FF));
    CHECK(brickColor(2) == nova::Color::fromHex(0xF72585FF));
    CHECK(brickColor(3) == nova::Color::fromHex(0xFFD166FF));
}
