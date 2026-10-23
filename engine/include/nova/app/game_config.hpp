#pragma once

#include <nova/core/math.hpp>

#include <memory>

namespace nova {

class Scene;

enum class Orientation : u8 { Portrait, Landscape };

using SceneFactory = std::unique_ptr<Scene> (*)();

template <class T>
std::unique_ptr<Scene> makeScene() {
    return std::make_unique<T>();
}

struct GameConfig {
    const char* title = "Nova Game";
    Vec2 designSize{720, 1280};
    Orientation orientation = Orientation::Portrait;
    const char* org = "gamestudio"; // save folder: <prefs>/<org>/<app>/
    const char* app = "game";
    Color clearColor = Color::Black;
    SceneFactory firstScene = nullptr;
};

// Defined by the game (exactly once).
GameConfig configureGame();

} // namespace nova
