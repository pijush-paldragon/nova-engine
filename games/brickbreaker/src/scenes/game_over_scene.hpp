#pragma once

#include <nova/nova.hpp>

// Final score; records a new best score in the save file.
class GameOverScene final : public nova::Scene {
public:
    GameOverScene(int score, int level) : score_(score), level_(level) {}
    void onEnter() override;
    void onUpdate(float dt) override;

private:
    int score_;
    int level_;
    float inputDelay_ = 0.5f; // ignore taps that were meant for the game
};
