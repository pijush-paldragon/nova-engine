#include "scenes/game_over_scene.hpp"

#include "scenes/menu_scene.hpp"
#include "scenes/play_scene.hpp"

#include <cstdio>

using namespace nova;

void GameOverScene::onEnter() {
    const i64 best = save().getInt("best_score");
    const bool newBest = score_ > best;
    if (newBest) {
        save().setInt("best_score", score_);
        if (auto r = save().flush(); !r) log::error("%s", r.error().message.c_str());
    }

    ui().load("ui/game_over.json");
    char text[48];
    std::snprintf(text, sizeof text, "Score %d  (level %d)", score_, level_);
    ui().label("score").setText(text);
    if (newBest) {
        ui().label("best").setText("NEW BEST!");
        ui().label("best").setStyle("highlight");
        ui().label("best").animate().scale(1.12f, 0.4f).ease(Ease::InOutQuad).yoyo().loop();
    } else {
        std::snprintf(text, sizeof text, "Best %lld", static_cast<long long>(best));
        ui().label("best").setText(text);
    }
    ui().onClick("again", [this] { scenes().replace<PlayScene>(); });
    ui().onClick("menu", [this] { scenes().replace<MenuScene>(); });

    if (options().autoplay) timers().after(1.f, [this] { scenes().replace<MenuScene>(); });
}

void GameOverScene::onUpdate(float) {
    if (input().keyPressed(Key::Space) || input().keyPressed(Key::Enter)) scenes().replace<MenuScene>();
}
