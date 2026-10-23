#include "systems.hpp"

#include "level.hpp"

#include <cmath>

using namespace nova;

namespace {

constexpr float kKeyboardPaddleSpeed = 900;
constexpr float kMinVerticalShare = 0.25f; // |dir.y| never drops below this

void burstAt(SystemContext& ctx, GameState& gs, Vec2 pos, Color color) {
    Entity e = gs.emitters[gs.nextEmitter++ % gs.emitters.size()];
    ctx.world.get<Transform>(e).pos = pos;
    auto& emitter = ctx.world.get<ParticleEmitter>(e);
    emitter.config.startColor = color;
    emitter.config.endColor = color.withAlpha(0);
    emitter.burst(24);
}

void hitBrick(SystemContext& ctx, GameState& gs, Entity brick, Brick& b) {
    --b.hp;
    Vec2 pos = ctx.world.get<Transform>(brick).pos;
    if (b.hp <= 0) {
        gs.score += 10 * gs.level;
        --gs.bricksLeft;
        ctx.audio.play(gs.breakSound, {.volume = 0.9f, .pitch = ctx.random.range(0.9f, 1.1f)});
        burstAt(ctx, gs, pos, ctx.world.get<Sprite>(brick).color);
        ctx.world.destroy(brick);
        return;
    }
    ctx.audio.play(gs.hitSound, {.volume = 0.7f, .pitch = 1.0f + 0.15f * float(b.hp)});
    ctx.world.get<Sprite>(brick).color = brickColor(b.hp);
    ctx.world.add<Tween>(brick, Tween::scale({1, 1}, {1.15f, 1.15f}, 0.07f, Ease::OutQuad).pingPong(1));
}

// Classic breakout control: where the ball hits the paddle sets its outgoing angle.
void bounceOffPaddle(SystemContext& ctx, GameState& gs) {
    float ballX = ctx.world.get<Transform>(gs.ball).pos.x;
    float paddleX = ctx.world.get<Transform>(gs.paddle).pos.x;
    float offset = clamp((ballX - paddleX) / (kPaddleSize.x * 0.5f), -1.f, 1.f);
    Vec2 dir = Vec2{offset * 0.8f, -1}.normalized();
    ctx.physics.setVelocity(gs.ball, dir * ctx.world.get<Ball>(gs.ball).speed);
    ctx.audio.play(gs.hitSound, {.volume = 0.5f, .pitch = 0.8f});
}

} // namespace

GameState& gameState(World& world) {
    for (auto [e, gs] : world.view<GameState>()) return gs;
    NOVA_VERIFY(false, "PlayScene has no GameState");
    static GameState fallback;
    return fallback;
}

void paddleControl(SystemContext& ctx) {
    GameState& gs = gameState(ctx.world);
    gs.time += ctx.dt;
    float x = ctx.world.get<Transform>(gs.paddle).pos.x;
    if (gs.autoplay) {
        // Track the ball with a wobble so it leaves the paddle at varied angles, and look away for
        // 2 s out of every 12 so smoke runs also exercise losing lives and game over.
        if (std::fmod(gs.time, 12.f) < 10.f) x = ctx.world.get<Transform>(gs.ball).pos.x + 50.f * std::sin(gs.time * 1.7f);
    } else if (const Pointer* p = ctx.input.primary(); p && p->held) {
        x = p->pos.x;
    } else {
        if (ctx.input.keyDown(Key::Left)) x -= kKeyboardPaddleSpeed * ctx.dt;
        if (ctx.input.keyDown(Key::Right)) x += kKeyboardPaddleSpeed * ctx.dt;
    }
    float half = kPaddleSize.x * 0.5f;
    ctx.physics.setTargetPosition(gs.paddle, {clamp(x, half, kDesignWidth - half), gs.paddleY});
}

void ballControl(SystemContext& ctx) {
    GameState& gs = gameState(ctx.world);
    Ball& ball = ctx.world.get<Ball>(gs.ball);
    if (!ball.launched) {
        Vec2 paddle = ctx.world.get<Transform>(gs.paddle).pos;
        ctx.physics.setPosition(gs.ball, {paddle.x, paddle.y - kPaddleSize.y * 0.5f - kBallRadius - 2});
        ctx.physics.setVelocity(gs.ball, {0, 0});
        return;
    }
    Vec2 v = ctx.physics.velocity(gs.ball);
    Vec2 dir = v.length() > 1 ? v.normalized() : Vec2{0.35f, -1}.normalized();
    if (std::abs(dir.y) < kMinVerticalShare) {
        dir.y = dir.y < 0 ? -kMinVerticalShare : kMinVerticalShare;
        dir = dir.normalized();
    }
    ctx.physics.setVelocity(gs.ball, dir * ball.speed);
}

void resolveContacts(SystemContext& ctx) {
    GameState& gs = gameState(ctx.world);
    for (const Contact& c : ctx.physics.contacts()) {
        if (!(c.a == gs.ball) && !(c.b == gs.ball)) continue;
        Entity other = c.a == gs.ball ? c.b : c.a;
        if (c.sensor) {
            if (ctx.world.has<DeathZone>(other)) gs.ballLost = true;
        } else if (Brick* brick = ctx.world.tryGet<Brick>(other)) {
            hitBrick(ctx, gs, other, *brick);
        } else if (ctx.world.has<Paddle>(other)) {
            bounceOffPaddle(ctx, gs);
        }
    }
}
