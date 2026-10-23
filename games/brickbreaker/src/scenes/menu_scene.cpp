#include "scenes/menu_scene.hpp"

#include "level.hpp"
#include "scenes/play_scene.hpp"

#include <cstdio>

using namespace nova;

namespace {

bool gMusicStarted = false;

Entity label(World& world, Vec2 pos, FontHandle font, const char* text, Color color = Color::White) {
    Entity e = world.spawn();
    world.add<Transform>(e, Transform{.pos = pos});
    world.add<TextLabel>(e, TextLabel{.font = font, .color = color, .align = TextAlign::Center}).set(text);
    return e;
}

} // namespace

void MenuScene::onEnter() {
    if (!gMusicStarted) {
        audio().setVolume(Bus::Music, float(save().getFloat("music_volume", 0.6)));
        audio().setVolume(Bus::Sfx, float(save().getFloat("sfx_volume", 1.0)));
        // Global scope: the music keeps playing across every scene.
        audio().playMusic(assets().music("music/loop.wav", kGlobalScope), {.loop = true, .fadeInMs = 800});
        gMusicStarted = true;
    }

    Atlas atlas = assets().atlas("sprites");
    FontHandle big = assets().font("fonts/Fredoka.ttf", 84);
    FontHandle medium = assets().font("fonts/Fredoka.ttf", 44);

    label(world(), {360, 360}, big, "BRICK");
    label(world(), {360, 450}, big, "BREAKER", Color::fromHex(0xFFD166FF));

    char best[32];
    std::snprintf(best, sizeof best, "Best: %lld", static_cast<long long>(save().getInt("best_score")));
    label(world(), {360, 560}, medium, best, Color::fromHex(0xAAB4D4FF));

    // A row of bricks as decoration.
    for (int col = 0; col < 8; ++col) {
        Rect r = brickRect(col, 0, 700);
        Entity b = world().spawn();
        world().add<Transform>(b, Transform{.pos = r.center()});
        world().add<Sprite>(b, Sprite{.region = atlas.region("brick"), .size = kBrickSize, .color = brickColor(3 - col % 3)});
    }

    Entity tap = label(world(), {360, 960}, medium, "Tap to play");
    world().add<Tween>(tap, Tween::alpha(1, 0.25f, 0.8f, Ease::InOutQuad).pingPong());

    if (options().autoplay) timers().after(0.5f, [this] { scenes().replace<PlayScene>(); });
}

void MenuScene::onUpdate(float) {
    if (input().tapped() || input().keyPressed(Key::Space) || input().keyPressed(Key::Enter)) {
        scenes().replace<PlayScene>();
    }
}
