#include "scenes/game_over_scene.hpp"

#include "scenes/menu_scene.hpp"
#include "scenes/play_scene.hpp"


using namespace nova;

void GameOverScene::onEnter() {
    const i64 best = save().getInt("best_score");
    const bool newBest = score_ > best;
    if (newBest) {
        save().setInt("best_score", score_);
        if (auto r = save().flush(); !r) log::error("%s", r.error().message.c_str());
    }

    ui().load("ui/game_over.json");
    ui().label("score").setKey("gameover.score", {{"score", i64(score_)}, {"level", i64(level_)}});
    if (newBest) {
        ui().label("best").setKey("gameover.newBest");
        ui().label("best").setStyle("highlight");
        ui().label("best").animate().scale(1.12f, 0.4f).ease(Ease::InOutQuad).yoyo().loop();
    } else {
        ui().label("best").setKey("gameover.best", {{"best", best}});
    }
    ui().onClick("again", [this] { scenes().replace<PlayScene>(); });
    ui().onClick("menu", [this] { scenes().replace<MenuScene>(); });

    if (options().autoplay) timers().after(1.f, [this] { scenes().replace<MenuScene>(); });
}

void GameOverScene::onUpdate(float) {
    if (input().keyPressed(Key::Space) || input().keyPressed(Key::Enter)) scenes().replace<MenuScene>();
}
