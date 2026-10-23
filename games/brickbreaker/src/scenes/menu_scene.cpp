#include "scenes/menu_scene.hpp"

using namespace nova;

void MenuScene::onEnter() {
    Atlas atlas = assets().atlas("sprites");
    ball_ = world().spawn();
    world().add<Transform>(ball_, Transform{.pos{360, 640}});
    world().add<Sprite>(ball_, Sprite{.region = atlas.region("ball"), .size{64, 64}});

    auto title = world().spawn();
    world().add<Transform>(title, Transform{.pos{360, 400}});
    world().add<TextLabel>(title, TextLabel{.font = assets().font("fonts/Fredoka.ttf", 72), .align = TextAlign::Center})
        .set("BRICK BREAKER");
}

void MenuScene::onUpdate(float dt) {
    world().get<Transform>(ball_).rotation += dt;
}
