#include <doctest/doctest.h>
#include <nova/core/random.hpp>
#include <nova/render/camera.hpp>
#include <nova/render/sprite_batch.hpp>

using namespace nova;

TEST_CASE("computeFit covers design area for 4:3, 16:9, 20:9") {
    for (auto win : {Vec2{1536, 2048}, Vec2{1080, 1920}, Vec2{1080, 2400}, Vec2{2400, 1080}}) {
        CAPTURE(win.x);
        CAPTURE(win.y);
        auto f = computeFit(win, {720, 1280});
        CHECK(f.visible.x <= 0.001f);
        CHECK(f.visible.y <= 0.001f);
        CHECK(f.visible.x + f.visible.w >= 719.999f);
        CHECK(f.visible.y + f.visible.h >= 1279.999f);
        CHECK(f.visible.w * f.scale == doctest::Approx(win.x));
        CHECK(f.visible.h * f.scale == doctest::Approx(win.y));
        // design area is centred along the long axis
        CHECK(f.visible.x + f.visible.w / 2 == doctest::Approx(360));
        CHECK(f.visible.y + f.visible.h / 2 == doctest::Approx(640));
    }
}

TEST_CASE("SpriteBatch merges same texture runs and orders by layer") {
    SpriteBatch b;
    b.begin();
    TextureHandle t1{0, 1}, t2{1, 1};
    auto cmd = [&](u8 layer, TextureHandle t, u16 order) {
        return SpriteCmd{makeSortKey(layer, BlendMode::Alpha, u16(t.index), order),
                         t, {0, 0, 0, 0}, Affine2::trs({0, 0}, 0, {1, 1}), {10, 10}, {0.5f, 0.5f},
                         Color::White, BlendMode::Alpha};
    };
    b.push(cmd(1, t1, 0));
    b.push(cmd(0, t2, 1));
    b.push(cmd(1, t1, 2));
    b.push(cmd(1, t2, 3));
    b.build([](TextureHandle) { return Vec2{64, 64}; });
    REQUIRE(b.runs().size() == 3); // [L0 t2] [L1 t1 x2] [L1 t2]
    CHECK(b.runs()[0].texture == t2);
    CHECK(b.runs()[1].texture == t1);
    CHECK(b.runs()[1].indexCount == 12);
    CHECK(b.vertices().size() == 16);
    CHECK(b.indices().size() == 24);
    CHECK(b.vertices()[0].pos.x == doctest::Approx(-5)); // pivot centred
    CHECK(b.vertices()[0].uv.x == doctest::Approx(0));   // src w=0 -> full texture
    CHECK(b.vertices()[2].uv.x == doctest::Approx(1));
}

TEST_CASE("SpriteBatch separates blend modes and maps src rect to UVs") {
    SpriteBatch b;
    b.begin();
    TextureHandle t{0, 1};
    b.push({makeSortKey(0, BlendMode::Alpha, 0, 0), t, {16, 0, 16, 32}, Affine2::trs({100, 50}, 0, {1, 1}),
            {16, 32}, {0, 0}, Color::fromHex(0xFF000080), BlendMode::Alpha});
    b.push({makeSortKey(0, BlendMode::Additive, 0, 1), t, {0, 0, 16, 32}, Affine2::trs({0, 0}, 0, {1, 1}),
            {16, 32}, {0, 0}, Color::White, BlendMode::Additive});
    b.build([](TextureHandle) { return Vec2{64, 32}; });
    REQUIRE(b.runs().size() == 2);
    CHECK(b.runs()[1].blend == BlendMode::Additive);
    const auto& v = b.vertices()[0];
    CHECK(v.pos.x == doctest::Approx(100)); // pivot (0,0) = top-left at position
    CHECK(v.pos.y == doctest::Approx(50));
    CHECK(v.uv.x == doctest::Approx(0.25f));
    CHECK(v.color.r == doctest::Approx(1.f));
    CHECK(v.color.a == doctest::Approx(128.f / 255.f));
}

TEST_CASE("Camera2D shake decays to zero and maps screen to world") {
    Camera2D cam;
    Random rng{3};
    cam.shake(10, 0.2f);
    cam.update(0.1f, rng);
    CHECK(cam.shakeOffset().length() <= 10.001f);
    cam.update(0.2f, rng);
    CHECK(cam.shakeOffset() == Vec2{0, 0});
    cam.position = {100, 0};
    CHECK(cam.screenToWorld({10, 10}).x == doctest::Approx(110));
    CHECK(cam.worldToScreen({110, 10}).x == doctest::Approx(10));
}
