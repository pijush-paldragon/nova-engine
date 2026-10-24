#pragma once

#include <nova/nova.hpp>

// Starter scene: a logo, the game's name and a Start button (ui/title.json). Replace it with
// your game.
class TitleScene final : public nova::Scene {
public:
    void onEnter() override;
    void onUpdate(float dt) override;

private:
    void start();

    nova::Entity logo_ = nova::kNullEntity;
};
