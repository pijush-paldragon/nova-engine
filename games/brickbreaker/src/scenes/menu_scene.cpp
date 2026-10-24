#include "scenes/menu_scene.hpp"

#include "level.hpp"
#include "scenes/play_scene.hpp"
#include "settings.hpp"

#include <cstdio>

using namespace nova;

namespace {

bool gMusicStarted = false;

} // namespace

void MenuScene::onEnter() {
    if (!gMusicStarted) {
        applyAudioSettings(audio(), save());
        // Global scope: the music keeps playing across every scene.
        audio().playMusic(assets().music("music/loop.wav", kGlobalScope), {.loop = true, .fadeInMs = 800});
        gMusicStarted = true;
    }

    // A row of bricks as decoration, between the title and the buttons.
    Atlas atlas = assets().atlas("sprites");
    for (int col = 0; col < 8; ++col) {
        Rect r = brickRect(col, 0, 700);
        Entity b = world().spawn();
        world().add<Transform>(b, Transform{.pos = r.center()});
        world().add<Sprite>(b, Sprite{.region = atlas.region("brick"), .size = kBrickSize, .color = brickColor(3 - col % 3)});
    }

    ui().load("ui/menu.json");
    char best[32];
    std::snprintf(best, sizeof best, "Best: %lld", static_cast<long long>(save().getInt("best_score")));
    ui().label("best").setText(best);
    ui().onClick("play", [this] { scenes().replace<PlayScene>(); });
    ui().onClick("settings", [this] { openSettings(ui(), audio(), save()); });

    if (options().autoplay) timers().after(0.5f, [this] { scenes().replace<PlayScene>(); });
}

void MenuScene::onUpdate(float) {
    if (ui().popupCount() == 0 && (input().keyPressed(Key::Space) || input().keyPressed(Key::Enter))) {
        scenes().replace<PlayScene>();
    }
}
