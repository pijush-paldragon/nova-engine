// Game-logic tests run on desktop with: ctest --preset macos-debug
// Test plain functions and components here; assets resolve from this game's assets/ folder.
#include <doctest/doctest.h>
#include <nova/nova.hpp>

TEST_CASE("game builds and links") {
    CHECK(nova::Vec2{3, 4}.length() == doctest::Approx(5));
}
