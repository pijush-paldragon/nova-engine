#include <app/debug_overlay.hpp>
#include <doctest/doctest.h>
#include <nova/app/app_options.hpp>
#include <nova/app/fixed_step.hpp>

using namespace nova;

TEST_CASE("FixedStepClock steps at 60Hz and reports alpha") {
    FixedStepClock c;
    CHECK(c.advance(1.0 / 60) == 1);
    CHECK(c.advance(0.5 / 60) == 0);
    CHECK(c.alpha() == doctest::Approx(0.5f));
    CHECK(c.advance(0.5 / 60) == 1);
    CHECK(c.advance(2.5 / 60) == 2); // 120 Hz display catching up after a slow frame
    CHECK(c.alpha() == doctest::Approx(0.5f));
}

TEST_CASE("FixedStepClock clamps huge frame and drops backlog") {
    FixedStepClock c;
    CHECK(c.advance(10.0) == 5); // e.g. resumed after 10 s in the background
    CHECK(c.advance(0.0) == 0);  // backlog dropped, no catch-up burst
    CHECK(c.alpha() < 1.0f);
    c.advance(0.9 / 60);
    c.reset();
    CHECK(c.alpha() == 0.f);
    CHECK(c.advance(0.5 / 60) == 0);
    CHECK(c.advance(-1.0) == 0); // clock going backwards is ignored
}

TEST_CASE("parseAppOptions") {
    const char* argv[] = {"bb", "--autoplay", "--frames", "1800", "--bogus", "--fail-on-alloc"};
    auto o = parseAppOptions(6, const_cast<char**>(argv));
    CHECK(o.autoplay);
    CHECK(o.maxFrames == 1800);
    CHECK(o.failOnAlloc);
    const char* bad[] = {"bb", "--frames"}; // missing value
    auto d = parseAppOptions(2, const_cast<char**>(bad));
    CHECK(d.maxFrames == -1);
    CHECK(!d.autoplay);
    const char* shot[] = {"bb", "--screenshot", "/tmp/x.png"};
    CHECK(parseAppOptions(3, const_cast<char**>(shot)).screenshotPath == "/tmp/x.png");
}

TEST_CASE("FpsCounter averages") {
    FpsCounter f;
    for (int i = 0; i < 60; ++i) f.add(1.f / 120);
    CHECK(f.fps() == doctest::Approx(120).epsilon(0.01));
    CHECK(f.avgMs() == doctest::Approx(8.333f).epsilon(0.01));
    FpsCounter empty;
    CHECK(empty.fps() == 0);
}
