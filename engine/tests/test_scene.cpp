#include <doctest/doctest.h>
#include <audio/audio_backend.hpp>
#include <input/input_internal.hpp>
#include <nova/assets/assets.hpp>
#include <nova/core/frame_arena.hpp>
#include <nova/core/random.hpp>
#include <nova/ecs/physics.hpp>
#include <nova/scene/scene.hpp>
#include <nova/scene/scene_stack.hpp>
#include <nova/scene/timers.hpp>
#include <scene/services.hpp>

#include <string>
#include <vector>

using namespace nova;
using V = std::vector<std::string>;

namespace {

V gLog;

struct Recorder : Scene {
    std::string name;
    explicit Recorder(std::string n, bool isOpaque = true) : name(std::move(n)) { opaque = isOpaque; }
    void onEnter() override { gLog.push_back(name + ":enter"); }
    void onExit() override { gLog.push_back(name + ":exit"); }
    void onPause() override { gLog.push_back(name + ":pause"); }
    void onResume() override { gLog.push_back(name + ":resume"); }
};

struct Loader : Scene {
    std::string scenePath, globalPath;
    Loader(std::string s, std::string g = {}) : scenePath(std::move(s)), globalPath(std::move(g)) {}
    void onEnter() override {
        assets().texture(scenePath);
        if (!globalPath.empty()) assets().texture(globalPath, kGlobalScope);
    }
};

// Fake texture backend: hands out sequential handles and records unloads by path.
struct FakeTextures {
    int loads = 0;
    V unloaded;
    std::vector<std::string> names;
    Assets makeAssets() {
        return Assets(
            [this](std::string_view path) -> std::optional<TextureHandle> {
                if (path.find("missing") != std::string_view::npos) return std::nullopt;
                ++loads;
                names.emplace_back(path);
                return TextureHandle{u32(names.size() - 1), 1};
            },
            [this](TextureHandle h) { unloaded.push_back(names[h.index]); }, TextureHandle{999, 1});
    }
};

} // namespace

TEST_CASE("SceneStack applies ops FIFO and refuses to pop last scene") {
    gLog.clear();
    FakeTextures fake;
    Assets assets = fake.makeAssets();
    Services services{};
    services.assets = &assets;
    SceneStack stack{services};
    stack.push<Recorder>("A");
    CHECK(gLog.empty()); // deferred
    stack.applyPending();
    CHECK(gLog == V{"A:enter"});
    stack.push<Recorder>("B");
    stack.replace<Recorder>("C");
    stack.applyPending();
    CHECK(gLog == V{"A:enter", "A:pause", "B:enter", "B:exit", "C:enter"});
    CHECK(stack.size() == 2);
    stack.pop();
    stack.pop();
    stack.applyPending();
    CHECK(stack.size() == 1);
    CHECK(gLog.back() == "A:resume"); // second pop refused
    stack.clear();
    CHECK(gLog.back() == "A:exit");
    CHECK(stack.empty());
}

TEST_CASE("SceneStack forEachVisible starts at topmost opaque scene") {
    FakeTextures fake;
    Assets assets = fake.makeAssets();
    Services services{};
    services.assets = &assets;
    SceneStack stack{services};
    stack.push<Recorder>("A");
    stack.push<Recorder>("B");
    stack.push<Recorder>("C", false);
    stack.applyPending();
    V visited;
    stack.forEachVisible([&](Scene& s) { visited.push_back(static_cast<Recorder&>(s).name); });
    CHECK(visited == V{"B", "C"});
    CHECK(static_cast<Recorder*>(stack.top())->name == "C");
}

TEST_CASE("SceneStack releases scene-scoped assets on exit, keeps global ones") {
    FakeTextures fake;
    Assets assets = fake.makeAssets();
    Services services{};
    services.assets = &assets;
    SceneStack stack{services};
    stack.push<Loader>("a.png", "g.png");
    stack.applyPending();
    CHECK(fake.loads == 2);
    stack.replace<Loader>("b.png");
    stack.applyPending();
    CHECK(fake.unloaded == V{"a.png"});
}

TEST_CASE("SceneStack replace with same asset reuses it without reload") {
    FakeTextures fake;
    Assets assets = fake.makeAssets();
    Services services{};
    services.assets = &assets;
    SceneStack stack{services};
    stack.push<Loader>("x.png");
    stack.applyPending();
    stack.replace<Loader>("x.png");
    stack.applyPending();
    CHECK(fake.loads == 1);
    CHECK(fake.unloaded.empty());
}

TEST_CASE("Assets atlas resolves regions and falls back for missing ones") {
    FakeTextures fake;
    Assets assets = fake.makeAssets();
    assets.setActiveScope(1);
    Atlas atlas = assets.atlas("sprites"); // reads the generated sprites.atlas
    REQUIRE(atlas.valid());
    SpriteRegion paddle = atlas.region("paddle");
    CHECK(paddle.src.w == 160);
    CHECK(paddle.texture.valid());
    CHECK(fake.names.back() == "sprites.png");
    SpriteRegion missing = atlas.region("nope");
    CHECK(missing.texture == TextureHandle{999, 1}); // checkerboard
    Atlas none = assets.atlas("missing_atlas");
    CHECK(!none.valid());
    CHECK(none.region("paddle").texture == TextureHandle{999, 1});
    CHECK(assets.texture("missing.png") == TextureHandle{999, 1});
}

TEST_CASE("Timers after/every/cancel including mutation during update") {
    Timers t;
    int a = 0, b = 0;
    TimerId idB = 0;
    t.after(0.5f, [&] {
        ++a;
        t.after(0.1f, [&] { ++a; });
        t.cancel(idB);
    });
    idB = t.every(0.2f, [&] { ++b; });
    t.update(0.25f);
    CHECK(b == 1);
    t.update(0.25f);
    CHECK(a == 1);
    CHECK(b == 1); // cancelled within the same update before firing
    t.update(0.1f);
    CHECK(a == 2);
    CHECK(t.active() == 0);
    t.every(1.f, [] {});
    t.clear();
    CHECK(t.active() == 0);
}

TEST_CASE("Systems run in registration order per phase") {
    static V order;
    order.clear();
    World w;
    InputBackend input;
    Physics physics{w};
    FakeTextures fake;
    Assets assets = fake.makeAssets();
    auto audio = AudioBackend::createUnavailable();
    Camera2D camera;
    FrameArena arena{1024};
    Random random{1};
    w.addSystem(Phase::Fixed, [](SystemContext&) { order.push_back("fixed1"); });
    w.addSystem(Phase::Update, [](SystemContext&) { order.push_back("update"); });
    w.addSystem(Phase::Fixed, [](SystemContext& ctx) { order.push_back(ctx.dt > 0 ? "fixed2" : "bad"); });
    SystemContext ctx{w, input, physics, assets, *audio, camera, arena, random, 1.f / 60};
    w.runSystems(Phase::Fixed, ctx);
    CHECK(order == V{"fixed1", "fixed2"});
    w.runSystems(Phase::Update, ctx);
    CHECK(order.back() == "update");
}

TEST_CASE("SceneStack applyPending reports whether a transition happened") {
    FakeTextures fake;
    Assets assets = fake.makeAssets();
    Services services{};
    services.assets = &assets;
    SceneStack stack{services};
    CHECK(!stack.applyPending());
    stack.push<Recorder>("A");
    CHECK(stack.applyPending());
    CHECK(!stack.applyPending());
}
