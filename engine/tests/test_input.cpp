#include <doctest/doctest.h>
#include <input/input_internal.hpp>

#include <SDL3/SDL_events.h>

using namespace nova;

namespace {

SDL_Event finger(SDL_EventType type, u64 id, f32 x, f32 y, f32 dx = 0, f32 dy = 0) {
    SDL_Event e{};
    e.type = type;
    e.tfinger.fingerID = id;
    e.tfinger.x = x;
    e.tfinger.y = y;
    e.tfinger.dx = dx;
    e.tfinger.dy = dy;
    return e;
}
SDL_Event fingerDown(u64 id, f32 x, f32 y) { return finger(SDL_EVENT_FINGER_DOWN, id, x, y); }
SDL_Event fingerUp(u64 id, f32 x, f32 y) { return finger(SDL_EVENT_FINGER_UP, id, x, y); }
SDL_Event fingerMove(u64 id, f32 x, f32 y, f32 dx, f32 dy) { return finger(SDL_EVENT_FINGER_MOTION, id, x, y, dx, dy); }

SDL_Event key(SDL_EventType type, SDL_Scancode sc) {
    SDL_Event e{};
    e.type = type;
    e.key.scancode = sc;
    e.key.down = type == SDL_EVENT_KEY_DOWN;
    return e;
}

SDL_Event mouseButton(SDL_EventType type, f32 x, f32 y) {
    SDL_Event e{};
    e.type = type;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = x;
    e.button.y = y;
    return e;
}

const ScreenFit kIdentityFit = computeFit({720, 1280}, {720, 1280});

} // namespace

TEST_CASE("Input press/hold/release lifecycle") {
    InputBackend in;
    in.beginFrame(0.016f);
    in.handleEvent(fingerDown(1, 0.5f, 0.5f), kIdentityFit);
    REQUIRE(in.primary());
    CHECK(in.primary()->pressed);
    CHECK(in.primary()->held);
    CHECK(in.primary()->pos.x == doctest::Approx(360));
    CHECK(in.primary()->pos.y == doctest::Approx(640));
    in.beginFrame(0.016f);
    CHECK(!in.primary()->pressed);
    CHECK(in.primary()->held);
    in.handleEvent(fingerUp(1, 0.5f, 0.5f), kIdentityFit);
    CHECK(in.primary()->released);
    CHECK(!in.primary()->held);
    CHECK(in.tapped());
    in.beginFrame(0.016f);
    CHECK(in.pointerCount() == 0);
    CHECK(in.primary() == nullptr);
    CHECK(!in.tapped());
}

TEST_CASE("Input drag is not a tap") {
    InputBackend in;
    in.beginFrame(0.016f);
    in.handleEvent(fingerDown(1, 0.5f, 0.5f), kIdentityFit);
    in.beginFrame(0.016f);
    in.handleEvent(fingerMove(1, 0.6f, 0.5f, 0.1f, 0), kIdentityFit);
    CHECK(in.primary()->delta.x == doctest::Approx(72));
    CHECK(in.primary()->pos.x == doctest::Approx(432));
    in.beginFrame(0.016f);
    CHECK(in.primary()->delta.x == doctest::Approx(0));
    in.handleEvent(fingerUp(1, 0.6f, 0.5f), kIdentityFit);
    CHECK(!in.tapped());
}

TEST_CASE("Input long press is not a tap") {
    InputBackend in;
    in.beginFrame(0.016f);
    in.handleEvent(fingerDown(1, 0.5f, 0.5f), kIdentityFit);
    in.beginFrame(0.5f);
    in.handleEvent(fingerUp(1, 0.5f, 0.5f), kIdentityFit);
    CHECK(!in.tapped());
}

TEST_CASE("Input primary pointer survives multi-touch") {
    InputBackend in;
    in.beginFrame(0.016f);
    in.handleEvent(fingerDown(1, 0.5f, 0.5f), kIdentityFit);
    in.beginFrame(0.016f);
    in.handleEvent(fingerDown(2, 0.2f, 0.2f), kIdentityFit);
    CHECK(in.pointerCount() == 2);
    CHECK(in.primary()->id == 1);
    in.beginFrame(0.016f);
    in.handleEvent(fingerMove(1, 0.6f, 0.5f, 0.1f, 0), kIdentityFit);
    in.handleEvent(fingerUp(1, 0.6f, 0.5f), kIdentityFit);
    CHECK(!in.tapped());
    CHECK(in.primary()->id == 2); // finger 2 still down takes over immediately
    in.beginFrame(0.016f);
    REQUIRE(in.primary());
    CHECK(in.primary()->id == 2);
    CHECK(!in.primary()->pressed); // no phantom press
    CHECK(in.pointerCount() == 1);
}

TEST_CASE("Input maps window coords through the same fit") {
    auto fit = computeFit({1536, 2048}, {720, 1280}); // 4:3 tablet, wider than design
    InputBackend in;
    in.beginFrame(0.016f);
    in.handleEvent(fingerDown(1, 0.0f, 0.5f), fit);
    CHECK(fit.visible.x < 0);
    CHECK(in.primary()->pos.x == doctest::Approx(fit.visible.x)); // left screen edge
    CHECK(in.primary()->pos.y == doctest::Approx(640));
}

TEST_CASE("Input mouse acts as a pointer using pixel density") {
    auto fit = computeFit({1440, 2560}, {720, 1280});
    fit.pixelDensity = 2; // window is 720x1280 points
    InputBackend in;
    in.beginFrame(0.016f);
    in.handleEvent(mouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 360, 640), fit);
    REQUIRE(in.primary());
    CHECK(in.primary()->id == kMousePointerId);
    CHECK(in.primary()->pos.x == doctest::Approx(360));
    in.handleEvent(mouseButton(SDL_EVENT_MOUSE_BUTTON_UP, 360, 640), fit);
    CHECK(in.tapped());
}

TEST_CASE("Input keys and back") {
    InputBackend in;
    in.beginFrame(0.016f);
    in.handleEvent(key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE), kIdentityFit);
    CHECK(in.keyPressed(Key::Space));
    CHECK(in.keyDown(Key::Space));
    in.beginFrame(0.016f);
    CHECK(!in.keyPressed(Key::Space));
    CHECK(in.keyDown(Key::Space));
    in.handleEvent(key(SDL_EVENT_KEY_UP, SDL_SCANCODE_SPACE), kIdentityFit);
    CHECK(!in.keyDown(Key::Space));
    in.handleEvent(key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_AC_BACK), kIdentityFit);
    CHECK(in.backPressed());
    in.beginFrame(0.016f);
    CHECK(!in.backPressed());
}

TEST_CASE("Input three-finger tap") {
    InputBackend in;
    in.beginFrame(0.016f);
    in.handleEvent(fingerDown(1, 0.1f, 0.1f), kIdentityFit);
    in.handleEvent(fingerDown(2, 0.2f, 0.1f), kIdentityFit);
    CHECK(!in.threeFingerTap());
    in.handleEvent(fingerDown(3, 0.3f, 0.1f), kIdentityFit);
    CHECK(in.threeFingerTap());
    in.beginFrame(0.016f);
    CHECK(!in.threeFingerTap());
}

TEST_CASE("Input cancelled touch releases the pointer without a tap") {
    InputBackend in;
    in.beginFrame(0.016f);
    in.handleEvent(fingerDown(1, 0.5f, 0.5f), kIdentityFit);
    in.beginFrame(0.016f);
    in.handleEvent(finger(SDL_EVENT_FINGER_CANCELED, 1, 0.5f, 0.5f), kIdentityFit); // e.g. a system gesture
    REQUIRE(in.primary());
    CHECK(in.primary()->released);
    CHECK(!in.tapped());
    in.beginFrame(0.016f);
    CHECK(in.pointerCount() == 0);
}
