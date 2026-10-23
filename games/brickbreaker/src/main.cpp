#include "scenes/menu_scene.hpp"

#include <nova/nova.hpp>

nova::GameConfig nova::configureGame() {
    return {
        .title = "Brick Breaker",
        .designSize = {720, 1280},
        .orientation = nova::Orientation::Portrait,
        .app = "brickbreaker",
        .clearColor = nova::Color::fromHex(0x14182BFF),
        .firstScene = nova::makeScene<MenuScene>,
    };
}
