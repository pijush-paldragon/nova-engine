#include <doctest/doctest.h>
#include <nova/core/log.hpp>

TEST_CASE("Smoke: log functions are callable") {
    nova::log::info("hello %d", 42);
    nova::log::warn("warn %s", "ok");
    CHECK(true);
}
