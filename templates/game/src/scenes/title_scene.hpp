#pragma once

#include <nova/nova.hpp>

// Starter scene: a logo, the game's name and "Tap anywhere". Replace it with your game.
class TitleScene final : public nova::Scene {
public:
    void onEnter() override;
    void onUpdate(float dt) override;

private:
    void tap();

    nova::Entity logo_ = nova::kNullEntity;
    nova::SoundHandle tapSound_;
};
