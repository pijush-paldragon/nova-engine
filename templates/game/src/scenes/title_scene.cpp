#include "scenes/title_scene.hpp"

using namespace nova;

void TitleScene::onEnter() {
    // Pre-size storage so gameplay frames never allocate (see docs/NEW_GAME.md in nova-engine).
    world().reserve<Transform, Sprite, TextLabel, Tween>(64);

    Atlas atlas = assets().atlas("sprites");
    tapSound_ = assets().sound("sfx/tap.wav");
    FontHandle titleFont = assets().font("fonts/Fredoka.ttf", 72);
    FontHandle hintFont = assets().font("fonts/Fredoka.ttf", 40);

    const Vec2 centre = renderer().visibleRect().center();

    logo_ = world().spawn();
    world().add<Transform>(logo_, Transform{.pos = centre - Vec2{0, 120}});
    world().add<Sprite>(logo_, Sprite{.region = atlas.region("logo"), .size{192, 192},
                                      .color = Color::fromHex(0x4CC9F0FF)});

    Entity title = world().spawn();
    world().add<Transform>(title, Transform{.pos = centre + Vec2{0, 70}});
    world().add<TextLabel>(title, TextLabel{.font = titleFont, .align = TextAlign::Center}).set("@GAME_NAME@");

    Entity hint = world().spawn();
    world().add<Transform>(hint, Transform{.pos = centre + Vec2{0, 160}});
    world().add<TextLabel>(hint, TextLabel{.font = hintFont, .color = Color::fromHex(0xAAB4D4FF),
                                           .align = TextAlign::Center}).set("Tap anywhere");
    world().add<Tween>(hint, Tween::alpha(1, 0.3f, 0.8f, Ease::InOutQuad).pingPong());

    // Smoke runs (--autoplay) exercise the tap path without input.
    if (options().autoplay) timers().every(0.5f, [this] { tap(); });
}

void TitleScene::onUpdate(float) {
    if (input().tapped() || input().keyPressed(Key::Space)) tap();
}

void TitleScene::tap() {
    audio().play(tapSound_);
    world().add<Tween>(logo_, Tween::scale({1, 1}, {1.2f, 1.2f}, 0.1f, Ease::OutQuad).pingPong(1));
    log::info("Tapped");
}
