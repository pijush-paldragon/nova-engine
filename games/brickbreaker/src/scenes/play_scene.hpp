#pragma once

#include "components.hpp"

// The game: paddle, ball, bricks, HUD. Pushes PauseScene when backgrounded or on back.
class PlayScene final : public nova::Scene {
public:
    void onEnter() override;
    void onExit() override;
    void onUpdate(float dt) override;
    void onBackground() override;
    void onPause() override { paused_ = true; }
    void onResume() override { paused_ = false; }

private:
    void spawnWalls(float topWallY);
    void spawnLevel();
    void launchBall();
    void updateHud();

    nova::Entity state_ = nova::kNullEntity;
    nova::Entity scoreLabel_ = nova::kNullEntity;
    nova::Entity livesLabel_ = nova::kNullEntity;
    nova::Entity levelLabel_ = nova::kNullEntity;
    nova::SpriteRegion brickRegion_;
    nova::SoundHandle loseSound_;
    float bricksTop_ = 220;
    bool waitingRespawn_ = false;
    bool levelClearing_ = false;
    bool paused_ = false;
    float autoLaunchDelay_ = 0;
};
