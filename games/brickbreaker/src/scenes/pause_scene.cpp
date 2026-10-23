#include "scenes/pause_scene.hpp"

using namespace nova;

void PauseScene::onEnter() {
    Rect visible = renderer().visibleRect();
    Entity shade = world().spawn();
    world().add<Transform>(shade, Transform{.pos = visible.center()});
    world().add<Sprite>(shade, Sprite{.region = assets().atlas("sprites").region("pixel"),
                                      .size{visible.w, visible.h}, .color = Color::fromHex(0x000000B0)});

    FontHandle big = assets().font("fonts/Fredoka.ttf", 84);
    FontHandle small = assets().font("fonts/Fredoka.ttf", 40);
    Entity title = world().spawn();
    world().add<Transform>(title, Transform{.pos{360, 560}});
    world().add<TextLabel>(title, TextLabel{.font = big, .align = TextAlign::Center}).set("PAUSED");
    Entity hint = world().spawn();
    world().add<Transform>(hint, Transform{.pos{360, 680}});
    world().add<TextLabel>(hint, TextLabel{.font = small, .color = Color::fromHex(0xAAB4D4FF), .align = TextAlign::Center})
        .set("Tap to resume");

    if (options().autoplay) timers().after(0.5f, [this] { scenes().pop(); });
}

void PauseScene::onUpdate(float) {
    if (input().tapped() || input().backPressed() || input().keyPressed(Key::P) || input().keyPressed(Key::Space)) {
        scenes().pop();
    }
}
