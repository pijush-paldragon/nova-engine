#include <doctest/doctest.h>
#include <render/renderer_backend.hpp>

#include <SDL3/SDL.h>

using namespace nova;

namespace {

struct HeadlessWindow {
    SDL_Window* window = nullptr;
    HeadlessWindow() {
        REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("test", 360, 640, 0);
        REQUIRE(window);
    }
    ~HeadlessWindow() {
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

} // namespace

TEST_CASE("RendererBackend loads textures, falls back to checkerboard, counts draw calls") {
    HeadlessWindow hw;
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "NOVA_RENDER_DRIVER", "software", true);
    auto created = RendererBackend::create(hw.window, {720, 1280}, Color::Black);
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "NOVA_RENDER_DRIVER");
    REQUIRE(created.ok());
    RendererBackend& r = *created.value();
    CHECK(std::string(r.backendName()) == "software");
    CHECK(r.visibleRect().w >= 719.f);
    CHECK(r.pixelScale() > 0);

    TextureHandle atlas = r.loadTexture("sprites.png");
    REQUIRE(atlas.valid());
    CHECK(r.textureSize(atlas) == Vec2{256, 256});
    CHECK(!r.loadTexture("nope.png").valid());
    CHECK(r.textureSize(r.checkerboard()) == Vec2{2, 2});

    SpriteBatch batch;
    batch.begin();
    TextureHandle stale{999, 7};
    batch.push({makeSortKey(0, BlendMode::Alpha, 0, 0), atlas, {}, Affine2::trs({100, 100}, 0, {1, 1}),
                {32, 32}, {0.5f, 0.5f}, Color::White, BlendMode::Alpha});
    batch.push({makeSortKey(1, BlendMode::Additive, 1, 1), stale, {}, Affine2::trs({200, 100}, 0, {1, 1}),
                {32, 32}, {0.5f, 0.5f}, Color::White, BlendMode::Additive});
    r.beginFrame();
    r.submit(batch); // stale texture draws the checkerboard instead of crashing
    r.endFrame();
    CHECK(r.drawCallsLastFrame() == 2);

    r.destroyTexture(atlas);
    CHECK(r.textureSize(atlas) == Vec2{2, 2}); // stale handle resolves to the checkerboard
    CHECK(r.liveTextureCount() == 1);          // only the checkerboard remains
}
