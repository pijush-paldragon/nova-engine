#include "scenes/play_scene.hpp"

#include "level.hpp"
#include "scenes/game_over_scene.hpp"
#include "scenes/pause_scene.hpp"
#include "systems.hpp"

#include <algorithm>
#include <cstdio>

using namespace nova;

namespace {

constexpr float kWallThickness = 20;
const Color kWallColor = Color::fromHex(0x2A3152FF);

void solidBox(World& world, Physics& physics, Vec2 center, Vec2 size) {
    Entity e = world.spawn();
    world.add<Transform>(e, Transform{.pos = center});
    physics.attach(e, BodyDef::staticBody().box(size).friction(0).restitution(1));
}

} // namespace

void PlayScene::onEnter() {
    physics().setGravity({0, 0});
    world().reserve<Transform, Sprite, Brick, RigidBody, Tween, TextLabel, ParticleEmitter>(256);

    Atlas atlas = assets().atlas("sprites");
    brickRegion_ = atlas.region("brick");
    loseSound_ = assets().sound("sfx/lose.wav");
    FontHandle hud = assets().font("fonts/Fredoka.ttf", 40);

    const Rect safe = renderer().safeArea();
    const Rect visible = renderer().visibleRect();
    const float hudY = std::max(safe.y, visible.y) + 60;
    const float topWallY = hudY + 60;
    bricksTop_ = std::max(kBricksTop, topWallY + 50);

    state_ = world().spawn();
    GameState& gs = world().add<GameState>(state_);
    gs.autoplay = options().autoplay;
    gs.hitSound = assets().sound("sfx/hit.wav");
    gs.breakSound = assets().sound("sfx/break.wav");
    gs.paddleY = std::min(1140.f, std::min(safe.bottom(), visible.bottom()) - 80);

    spawnWalls(topWallY);

    gs.paddle = world().spawn();
    world().add<Transform>(gs.paddle, Transform{.pos{360, gs.paddleY}});
    world().add<Sprite>(gs.paddle, Sprite{.region = atlas.region("paddle"), .size = kPaddleSize,
                                          .color = Color::fromHex(0xE8EAF6FF), .layer = 10});
    world().add<Paddle>(gs.paddle);
    physics().attach(gs.paddle, BodyDef::kinematic().box(kPaddleSize).friction(0).restitution(1));

    gs.ball = world().spawn();
    world().add<Transform>(gs.ball, Transform{.pos{360, gs.paddleY - 40}});
    world().add<Sprite>(gs.ball, Sprite{.region = atlas.region("ball"), .size{kBallRadius * 2, kBallRadius * 2}, .layer = 20});
    world().add<Ball>(gs.ball);
    physics().attach(gs.ball, BodyDef::dynamic().circle(kBallRadius).bullet().restitution(1).friction(0).fixedRotation());

    EmitterConfig burst{.region = atlas.region("particle"), .lifetime{0.3f, 0.7f}, .speed{80, 340},
                        .size{16, 2}, .gravity{0, 700}};
    for (Entity& e : gs.emitters) {
        e = world().spawn();
        world().add<Transform>(e);
        world().add<ParticleEmitter>(e, makeEmitter(burst, 48));
    }

    scoreLabel_ = world().spawn();
    world().add<Transform>(scoreLabel_, Transform{.pos{std::max(safe.x, 0.f) + 24, hudY}});
    world().add<TextLabel>(scoreLabel_, TextLabel{.font = hud, .align = TextAlign::Left});
    levelLabel_ = world().spawn();
    world().add<Transform>(levelLabel_, Transform{.pos{360, hudY}});
    world().add<TextLabel>(levelLabel_, TextLabel{.font = hud, .color = Color::fromHex(0xAAB4D4FF), .align = TextAlign::Center});
    livesLabel_ = world().spawn();
    world().add<Transform>(livesLabel_, Transform{.pos{std::min(safe.right(), kDesignWidth) - 24, hudY}});
    world().add<TextLabel>(livesLabel_, TextLabel{.font = hud, .align = TextAlign::Right});

    spawnLevel();
    updateHud();

    world().addSystem(Phase::Fixed, paddleControl);
    world().addSystem(Phase::Fixed, ballControl);
    world().addSystem(Phase::Update, resolveContacts);
}

void PlayScene::spawnWalls(float topWallY) {
    // Gameplay walls sit on the design area's edges, so play is identical on every aspect ratio.
    const float tall = 4000;
    solidBox(world(), physics(), {-kWallThickness * 0.5f, 640}, {kWallThickness, tall});
    solidBox(world(), physics(), {kDesignWidth + kWallThickness * 0.5f, 640}, {kWallThickness, tall});
    solidBox(world(), physics(), {360, topWallY - kWallThickness * 0.5f}, {kDesignWidth + 2 * kWallThickness, kWallThickness});

    Entity zone = world().spawn();
    world().add<Transform>(zone, Transform{.pos{360, renderer().visibleRect().bottom() + 60}});
    world().add<DeathZone>(zone);
    physics().attach(zone, BodyDef::staticBody().box({4000, 40}).sensor());

    // Thin visible borders.
    SpriteRegion pixel = assets().atlas("sprites").region("pixel");
    auto line = [&](Vec2 center, Vec2 size) {
        Entity e = world().spawn();
        world().add<Transform>(e, Transform{.pos = center});
        world().add<Sprite>(e, Sprite{.region = pixel, .size = size, .color = kWallColor});
    };
    const float bottom = renderer().visibleRect().bottom();
    line({2, (topWallY + bottom) * 0.5f}, {4, bottom - topWallY});
    line({kDesignWidth - 2, (topWallY + bottom) * 0.5f}, {4, bottom - topWallY});
    line({360, topWallY - 2}, {kDesignWidth, 4});
}

void PlayScene::spawnLevel() {
    memory::LoadingScope loading; // building a level mid-scene is loading, not per-frame gameplay
    GameState& gs = gameState(world());
    LevelLayout layout = makeLevel(gs.level);
    gs.bricksLeft = 0;
    for (int row = 0; row < layout.rows; ++row) {
        for (int col = 0; col < layout.cols; ++col) {
            int hp = layout.hp[size_t(row * layout.cols + col)];
            Rect r = brickRect(col, row, bricksTop_);
            Entity b = world().spawn();
            world().add<Transform>(b, Transform{.pos = r.center()});
            world().add<Sprite>(b, Sprite{.region = brickRegion_, .size = kBrickSize, .color = brickColor(hp), .layer = 5});
            world().add<Brick>(b, Brick{hp});
            physics().attach(b, BodyDef::staticBody().box(kBrickSize).friction(0).restitution(1));
            ++gs.bricksLeft;
        }
    }
    world().get<Ball>(gs.ball).launched = false;
    autoLaunchDelay_ = 0.6f;
}

void PlayScene::launchBall() {
    GameState& gs = gameState(world());
    Ball& ball = world().get<Ball>(gs.ball);
    ball.launched = true;
    float side = random().range(0, 1) < 0.5f ? -1.f : 1.f;
    physics().setVelocity(gs.ball, Vec2{0.35f * side, -1}.normalized() * ball.speed);
}

void PlayScene::updateHud() {
    GameState& gs = gameState(world());
    char text[32];
    std::snprintf(text, sizeof text, "Score %d", gs.score);
    world().get<TextLabel>(scoreLabel_).set(text);
    std::snprintf(text, sizeof text, "Level %d", gs.level);
    world().get<TextLabel>(levelLabel_).set(text);
    std::snprintf(text, sizeof text, "Lives %d", gs.lives);
    world().get<TextLabel>(livesLabel_).set(text);
}

void PlayScene::onUpdate(float dt) {
    GameState& gs = gameState(world());
    Ball& ball = world().get<Ball>(gs.ball);

    if (input().backPressed() || input().keyPressed(Key::P)) {
        scenes().push<PauseScene>();
        return;
    }

    if (gs.ballLost) {
        gs.ballLost = false;
        ball.launched = false;
        --gs.lives;
        log::info("Ball lost: %d lives left", gs.lives);
        audio().play(loseSound_);
        camera().shake(12, 0.25f);
        if (gs.lives <= 0) {
            scenes().replace<GameOverScene>(gs.score, gs.level);
        } else {
            waitingRespawn_ = true;
            autoLaunchDelay_ = 0.8f;
            timers().after(0.6f, [this] { waitingRespawn_ = false; });
        }
    }

    if (gs.bricksLeft == 0 && !levelClearing_) {
        levelClearing_ = true;
        ball.launched = false;
        timers().after(0.8f, [this] {
            GameState& g = gameState(world());
            ++g.level;
            world().get<Ball>(g.ball).speed = kBallBaseSpeed + kBallSpeedPerLevel * float(g.level - 1);
            spawnLevel();
            levelClearing_ = false;
        });
    }

    if (!ball.launched && !waitingRespawn_ && !levelClearing_) {
        autoLaunchDelay_ -= dt;
        bool launch = gs.autoplay ? autoLaunchDelay_ <= 0 : (input().tapped() || input().keyPressed(Key::Space));
        if (launch) launchBall();
    }

    updateHud();
}

void PlayScene::onBackground() {
    if (!paused_) scenes().push<PauseScene>();
}

void PlayScene::onExit() {
    GameState& gs = gameState(world());
    log::info("Play ended: level %d, score %d", gs.level, gs.score);
}
