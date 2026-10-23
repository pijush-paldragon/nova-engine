#include "scenes/game_over_scene.hpp"

#include "scenes/menu_scene.hpp"

#include <cstdio>

using namespace nova;

void GameOverScene::onEnter() {
    const i64 best = save().getInt("best_score");
    const bool newBest = score_ > best;
    if (newBest) {
        save().setInt("best_score", score_);
        if (auto r = save().flush(); !r) log::error("%s", r.error().message.c_str());
    }

    FontHandle big = assets().font("fonts/Fredoka.ttf", 84);
    FontHandle medium = assets().font("fonts/Fredoka.ttf", 48);
    auto label = [&](Vec2 pos, FontHandle font, const char* text, Color color) {
        Entity e = world().spawn();
        world().add<Transform>(e, Transform{.pos = pos});
        world().add<TextLabel>(e, TextLabel{.font = font, .color = color, .align = TextAlign::Center}).set(text);
        return e;
    };

    label({360, 420}, big, "GAME OVER", Color::fromHex(0xF72585FF));
    char text[48];
    std::snprintf(text, sizeof text, "Score %d  (level %d)", score_, level_);
    label({360, 560}, medium, text, Color::White);
    if (newBest) {
        Entity banner = label({360, 660}, medium, "NEW BEST!", Color::fromHex(0xFFD166FF));
        world().add<Tween>(banner, Tween::scale({1, 1}, {1.12f, 1.12f}, 0.4f, Ease::InOutQuad).pingPong());
    } else {
        std::snprintf(text, sizeof text, "Best %lld", static_cast<long long>(best));
        label({360, 660}, medium, text, Color::fromHex(0xAAB4D4FF));
    }
    label({360, 900}, medium, "Tap to continue", Color::fromHex(0xAAB4D4FF));

    if (options().autoplay) timers().after(1.f, [this] { scenes().replace<MenuScene>(); });
}

void GameOverScene::onUpdate(float dt) {
    inputDelay_ -= dt;
    if (inputDelay_ <= 0 && (input().tapped() || input().keyPressed(Key::Space))) scenes().replace<MenuScene>();
}
