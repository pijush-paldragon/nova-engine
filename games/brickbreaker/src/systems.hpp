#pragma once

#include "components.hpp"

// The play scene's single GameState.
GameState& gameState(nova::World& world);

// Fixed step: move the paddle toward the finger (or the ball, in autoplay).
void paddleControl(nova::SystemContext& ctx);
// Fixed step: keep the ball on the paddle until launched, then at constant speed and never
// travelling too horizontally.
void ballControl(nova::SystemContext& ctx);
// Per frame: react to this frame's contacts (bricks, paddle, death zone).
void resolveContacts(nova::SystemContext& ctx);
