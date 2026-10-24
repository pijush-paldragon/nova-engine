#include "support/ui_fixture.hpp"

#include <doctest/doctest.h>
#include <nova/assets/assets.hpp>
#include <nova/scene/scene.hpp>
#include <nova/scene/scene_stack.hpp>
#include <nova/ui/ui.hpp>
#include <scene/services.hpp>
#include <ui/ui_backend.hpp>

using namespace nova;
using namespace nova::ui;

TEST_CASE("Canvas builds a tree from code and finds elements by id") {
    Canvas canvas;
    PanelRef box = canvas.root().add<Panel>({.id = "box"});
    ButtonRef ok = box.add<Button>({.id = "ok", .text = "OK"});
    CHECK(ok.valid());
    CHECK(canvas.button("ok").valid());
    CHECK(canvas.button("ok").handle() == ok.handle());
    CHECK(canvas.panel("box").valid());
    CHECK(canvas.elementCount() == 3); // root + box + ok
}

TEST_CASE("Path ids and duplicate-free lookup") {
    Canvas canvas;
    canvas.root().add<Label>({.id = "settings/volume"});
    CHECK(canvas.label("settings/volume").valid());
    CHECK(!canvas.label("settings").valid());
}

TEST_CASE("Missing id or wrong kind gives an empty ref that ignores calls") {
    testing::LogCapture logs;
    Canvas canvas;
    canvas.root().add<Button>({.id = "ok"});
    ButtonRef missing = canvas.button("nope");
    CHECK(!missing.valid());
    missing.setText("x"); // no crash
    missing.setVisible(false);
    CHECK(missing.box().w == 0);
    CHECK(!canvas.label("ok").valid()); // it's a button, not a label
    canvas.button("nope");               // second lookup: no second message
    CHECK(logs.count("ui: no button \"nope\" in code") == 1);
    CHECK(logs.count("ui: no label \"ok\" in code") == 1);
}

TEST_CASE("Removing an element frees its subtree at flush") {
    Canvas canvas;
    PanelRef panel = canvas.root().add<Panel>({.id = "panel"});
    LabelRef a = panel.add<Label>({.id = "a"});
    panel.add<Label>({.id = "b"});
    CHECK(canvas.elementCount() == 4);
    panel.remove();
    CHECK(!canvas.label("a").valid()); // gone from lookups immediately
    CHECK(!a.valid());
    CHECK(canvas.elementCount() == 4); // storage freed at flush
    uiFlush(canvas);
    CHECK(canvas.elementCount() == 1);
    CHECK(!panel.valid());
}

TEST_CASE("Stale handle after slot reuse is rejected") {
    Canvas canvas;
    LabelRef old = canvas.root().add<Label>({.id = "x"});
    UiHandle stale = old.handle();
    old.remove();
    uiFlush(canvas);
    LabelRef fresh = canvas.root().add<Label>({.id = "y"});
    CHECK(fresh.handle().index == stale.index); // same slot reused
    CHECK(!old.valid());
    old.setText("ignored"); // must not write into "y"
    CHECK(fresh.valid());
}

namespace {

struct UiScene : Scene {
    u32 count = 0;
    void onEnter() override {
        ui().root().add<Label>({.id = "hello"});
        count = ui().elementCount();
    }
    u32 liveCount() { return ui().elementCount(); }
};

} // namespace

TEST_CASE("Scene exposes its own canvas") {
    Assets assets([](std::string_view) -> std::optional<TextureHandle> { return TextureHandle{1, 1}; },
                  [](TextureHandle) {}, TextureHandle{});
    Services services{};
    services.assets = &assets;
    SceneStack stack{services};
    stack.push<UiScene>();
    stack.applyPending();
    auto* first = static_cast<UiScene*>(stack.top());
    CHECK(first->count == 2);
    stack.push<UiScene>();
    stack.applyPending();
    auto* second = static_cast<UiScene*>(stack.top());
    CHECK(second != first);
    CHECK(second->liveCount() == 2); // its own tree, not the first scene's
    CHECK(first->liveCount() == 2);
    stack.clear();
}
