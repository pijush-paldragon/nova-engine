# Starting a new game on Nova

## 1. Create the folder

```
games/mygame/
├─ CMakeLists.txt
├─ assets/            everything the game loads (paths are relative to this folder)
└─ src/
   ├─ main.cpp        configureGame()
   └─ scenes/...
```

`games/mygame/CMakeLists.txt`:

```cmake
nova_add_game(mygame
    SOURCES src/main.cpp src/scenes/title_scene.cpp
    ASSETS assets
    BUNDLE_ID com.gamestudio.mygame
    DISPLAY_NAME "My Game"
    ORIENTATION Portrait)          # or Landscape
```

## 2. Select it

- Desktop / iOS: configure with `-DNOVA_GAME=mygame`, e.g. `cmake --preset macos-debug -DNOVA_GAME=mygame`.
- Android: in `platforms/android/gradle.properties` set `novaGame=mygame`, `novaApplicationId=com.gamestudio.mygame` and `novaAppName=My Game`.

## 3. Write it

```cpp
// src/main.cpp
#include "scenes/title_scene.hpp"
#include <nova/nova.hpp>

nova::GameConfig nova::configureGame() {
    return {.title = "My Game", .designSize = {720, 1280}, .app = "mygame",
            .clearColor = nova::Color::fromHex(0x101020FF), .firstScene = nova::makeScene<TitleScene>};
}
```

```cpp
// src/scenes/title_scene.hpp
#pragma once
#include <nova/nova.hpp>

struct Spinner { float speed; };              // components are plain structs

void spin(nova::SystemContext& ctx) {          // systems are plain functions
    for (auto [e, t, s] : ctx.world.view<nova::Transform, Spinner>()) t.rotation += s.speed * ctx.dt;
}

class TitleScene final : public nova::Scene {
public:
    void onEnter() override {
        using namespace nova;
        Atlas atlas = assets().atlas("sprites");              // assets/sprites.atlas + its PNG
        Entity logo = world().spawn();
        world().add<Transform>(logo, Transform{.pos{360, 640}});
        world().add<Sprite>(logo, Sprite{.region = atlas.region("logo"), .size{256, 256}});
        world().add<Spinner>(logo, Spinner{1.5f});
        world().addSystem(Phase::Update, spin);
        tap_ = assets().sound("sfx/tap.wav");
    }
    void onUpdate(float) override {
        if (input().tapped()) audio().play(tap_);
    }
private:
    nova::SoundHandle tap_;
};
```

## The rules of the engine

- **Include only `<nova/...>`.** SDL, Box2D and SDL_mixer/ttf headers aren't on a game's include path, by design.
- **Coordinates are design units**, y-down, origin at the top-left of the design area (`designSize`). The visible area extends past it on the long axis (`renderer().visibleRect()`); keep HUD inside `renderer().safeArea()`.
- **Scenes own their world.** Entities, physics bodies, timers and the camera die with the scene. `scenes().push/pop/replace` apply at the end of the frame.
- **Assets are tied to the scene that loads them** and freed when it exits. Pass `nova::kGlobalScope` for things that live for the whole game (music, shared fonts).
- **Destroying is deferred:** `world().destroy(e)` keeps `e` valid until the end of the frame.
- **Fixed vs Update:** `Phase::Fixed` systems and `onFixedUpdate` run at 60 Hz after physics; `Phase::Update`, `onUpdate`, timers and tweens run once per rendered frame. Read `physics().contacts()` in either.
- **Don't allocate per frame.** Call `world().reserve<Components...>(n)` in `onEnter`, pool things like particle emitters, and use `char` buffers + `TextLabel::set` for changing text. Wrap mid-scene loading (a new level) in `nova::memory::LoadingScope`. Check with `--autoplay --frames N --fail-on-alloc`.
- **Missing assets don't crash:** textures show a magenta checkerboard, sounds stay silent, and the log says what's missing.

The sample game in `games/brickbreaker` uses every one of these patterns.
