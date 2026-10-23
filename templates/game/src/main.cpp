#include "scenes/title_scene.hpp"

#include <nova/nova.hpp>

nova::GameConfig nova::configureGame() {
    return {
        .title = "@GAME_NAME@",
        .designSize = {@DESIGN_W@, @DESIGN_H@},
        .orientation = nova::Orientation::@ORIENTATION@,
        .org = "@SAVE_ORG@",   // save data lives in <app data>/<org>/<app>/
        .app = "@GAME_TARGET@",
        .clearColor = nova::Color::fromHex(0x141A2EFF),
        .firstScene = nova::makeScene<TitleScene>,
    };
}
