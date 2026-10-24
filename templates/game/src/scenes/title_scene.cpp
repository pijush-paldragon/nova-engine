#include "scenes/title_scene.hpp"

using namespace nova;

void TitleScene::onEnter() {
    // Pre-size storage so gameplay frames never allocate (see docs/NEW_GAME.md in nova-engine).
    world().reserve<Transform, Sprite, Tween>(64);

    Atlas atlas = assets().atlas("sprites");
    const Vec2 centre = renderer().visibleRect().center();
    logo_ = world().spawn();
    world().add<Transform>(logo_, Transform{.pos = centre - Vec2{0, 160}});
    world().add<Sprite>(logo_, Sprite{.region = atlas.region("logo"), .size{192, 192},
                                      .color = Color::fromHex(0x4CC9F0FF)});

    // The UI comes from assets/ui/title.json, styled by assets/ui/theme.json.
    ui().load("ui/title.json");
    ui().label("title").setText("@GAME_NAME@");
    ui().onClick("start", [this] { start(); });
    ui().button("start").animate().scale(1.05f, 0.8f).ease(Ease::InOutQuad).yoyo().loop();

    // Smoke runs (--autoplay) exercise the same path as the button.
    if (options().autoplay) timers().every(0.5f, [this] { start(); });
}

void TitleScene::onUpdate(float) {
    if (input().keyPressed(Key::Space) || input().keyPressed(Key::Enter)) start();
}

void TitleScene::start() {
    // The button's click sound comes from its style; this is where your game would begin.
    world().add<Tween>(logo_, Tween::scale({1, 1}, {1.2f, 1.2f}, 0.1f, Ease::OutQuad).pingPong(1));
    log::info("Start pressed");
}
