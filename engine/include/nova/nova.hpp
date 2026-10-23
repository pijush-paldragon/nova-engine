#pragma once

// Umbrella header: everything a game needs from the engine.

#include <nova/app/app_options.hpp>
#include <nova/app/game_config.hpp>
#include <nova/assets/assets.hpp>
#include <nova/core/assert.hpp>
#include <nova/core/log.hpp>
#include <nova/core/math.hpp>
#include <nova/core/random.hpp>
#include <nova/core/types.hpp>
#include <nova/ecs/components.hpp>
#include <nova/ecs/particles.hpp>
#include <nova/ecs/physics.hpp>
#include <nova/ecs/system.hpp>
#include <nova/ecs/tween.hpp>
#include <nova/ecs/world.hpp>
#include <nova/input/input.hpp>
#include <nova/physics/body_def.hpp>
#include <nova/render/camera.hpp>
#include <nova/render/renderer.hpp>
#include <nova/save/save_store.hpp>
#include <nova/scene/scene.hpp>
#include <nova/scene/scene_stack.hpp>
#include <nova/scene/timers.hpp>
