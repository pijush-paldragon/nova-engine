#pragma once

#include <nova/nova.hpp>

#include <array>

inline constexpr float kDesignWidth = 720;
inline constexpr nova::Vec2 kPaddleSize{160, 32};
inline constexpr float kBallRadius = 16;
inline constexpr float kBallBaseSpeed = 720;
inline constexpr float kBallSpeedPerLevel = 40;

struct Paddle {};
struct Ball {
    float speed = kBallBaseSpeed;
    bool launched = false;
};
struct Brick {
    int hp = 1;
};
struct DeathZone {};

// Shared by the play scene and its systems; carried by a single entity.
struct GameState {
    int score = 0;
    int lives = 3;
    int level = 1;
    int bricksLeft = 0;
    bool ballLost = false; // set by systems, handled by PlayScene
    bool autoplay = false;
    float time = 0;
    float paddleY = 1140;
    nova::Entity paddle = nova::kNullEntity;
    nova::Entity ball = nova::kNullEntity;
    nova::SoundHandle hitSound;
    nova::SoundHandle breakSound;
    std::array<nova::Entity, 8> emitters{}; // pooled particle bursts, reused round-robin
    unsigned nextEmitter = 0;
};
