# Making games with Nova

Your games live in their own folders (their own git repos) **next to** the engine, and every build compiles the engine's current source. There's no engine copy inside a game and no version to bump: improve the engine once and every game picks it up on its next build.

```
GameStudio/
├─ nova-engine/     the engine (this repo)
├─ MyGame/          a game
└─ AnotherGame/     another game
```

## 1. Create a game

From `GameStudio/`:

```bash
python3 nova-engine/tools/new_game.py MyGame --name "My Game" --bundle-id com.studio.mygame
```

| Option | Meaning |
|---|---|
| `--name` | Display name (1–30 characters; no `" < > & \ $ ;`) |
| `--bundle-id` | App id on both stores, e.g. `com.studio.mygame` (letters and digits per segment: iOS rejects `_`, Android rejects `-`) |
| `--orientation portrait\|landscape` | Default portrait (720×1280 design size); landscape is 1280×720 |
| `--target` | CMake/executable name; default is the name lowercased with everything but letters and digits removed (`My Game!` → `mygame`). Needed when that starts with a digit or is reserved (`nova`, `test`, `main`, `sdl…`, …) |
| `--no-git` | Skip `git init` and the initial commit |

The generator refuses a folder that already has files in it, or one inside the engine repo, and creates:

```
MyGame/
├─ CMakeLists.txt        finds the engine, then nova_add_game(...) and nova_add_game_tests(...)
├─ CMakePresets.json     macos-debug, macos-release, ios
├─ src/main.cpp          configureGame(): title, design size, orientation, first scene
├─ src/scenes/           a starter TitleScene (logo, title, "Tap anywhere")
├─ tests/test_game.cpp   game-logic tests (desktop)
├─ assets/               starter atlas, sound, font (Fredoka, OFL)
└─ platforms/
   ├─ ios/               Info.plist.in, LaunchScreen.storyboard, Assets.xcassets/AppIcon (yours to edit)
   └─ android/           Gradle project: app/build.gradle holds the app's identity and signing
```

## 2. Build and run

From inside the game folder:

| Target | Commands |
|---|---|
| macOS | `cmake --preset macos-debug && cmake --build --preset macos-debug`, then `build/macos-debug/mygame.app/Contents/MacOS/mygame` |
| Tests | `ctest --preset macos-debug` |
| Smoke test | `build/macos-debug/mygame.app/Contents/MacOS/mygame --autoplay --frames 600 --fail-on-alloc` |
| iOS | `cmake --preset ios && open build/ios/mygame.xcodeproj`, choose the `mygame` scheme, set your Team, Run |
| Android | `cd platforms/android && ./gradlew installDebug` (`ANDROID_SERIAL=<serial>` with several devices) |

The first build compiles SDL and the engine into the game's `build/` folder (a couple of minutes); after that builds are incremental. Nothing is ever written into the engine folder.

### Where the engine is found

Re-checked on every configure/build, in this order:

1. An explicit override: `-DNOVA_ENGINE_DIR=/path` (CMake; it sticks in that build folder until you clear it with `-DNOVA_ENGINE_DIR=`) or `-PnovaEngineDir=/path` (Gradle; also settable in `platforms/android/gradle.properties`; relative paths are from the game folder).
2. The `NOVA_ENGINE_DIR` environment variable (CMake and Gradle).
3. The path recorded when the game was created (`../nova-engine` for the layout above).

Moving or renaming the engine folder therefore only needs the variable or override updated — build folders don't keep a stale engine path. If the engine isn't found, the build stops with a message saying where it looked.

## 3. Working with the live engine

- After changing the engine, rebuild **each** game and run its smoke test (`--autoplay --frames 600 --fail-on-alloc`). An engine change reaches every game immediately, including ones you haven't touched in months, so catch breakage early.
- Brick Breaker (`nova-engine/games/brickbreaker`) is built the same way as your games and is the engine's regression test: run `ctest --preset macos-debug` and its 4200-frame smoke test in the engine repo before pushing engine changes.
- **Game-owned copies.** The engine's code, CMake and shared Android build logic (`platforms/android/nova-app.gradle`) reach every game automatically. A few files were *copied* into each game when it was created and are now yours: `platforms/ios/Info.plist.in` and `LaunchScreen.storyboard`, and in `platforms/android/` the `AndroidManifest.xml`, root `build.gradle` (Android Gradle Plugin version) and the Gradle wrapper. If an engine update changes these (e.g. an SDL upgrade that needs a newer plugin), compare your copy with `nova-engine/templates/game/` and apply the change.

## 4. Sprite atlases

Put each sprite in its own image file (PNG with transparency is best) in a folder outside `assets/`, e.g. `art/gameplay/`, then pack the folder:

```bash
python3 ../nova-engine/tools/pack_atlas.py art/gameplay -o assets/gameplay
```

That writes `assets/gameplay.png` and `assets/gameplay.atlas`, choosing the smallest power-of-two image size (up to 2048×2048) that fits every sprite. Each region is named after its file; subfolders become prefixes (`art/gameplay/ui/button.png` → `"ui/button"`). In code:

```cpp
nova::Atlas atlas = assets().atlas("gameplay");
world().add<nova::Sprite>(e, nova::Sprite{.region = atlas.region("ui/button"), .size{200, 80}});
```

- Re-run the command whenever the art changes, and commit both output files (`.atlas` is generated — don't edit it by hand).
- Options: `--padding 2` (pixels of repeated edge colour around each sprite, so smooth scaling never bleeds a neighbour in), `--max-size 2048` (largest atlas side), `--scale 0.5` (shrink every sprite first, e.g. to ship 2× art at 1×).
- Group sprites that appear on screen together into one atlas (one per scene or for UI): sprites from the same atlas on the same layer draw in a single batch. If a folder doesn't fit, split it or lower `--scale`.
- File names may use letters, digits, `_`, `.` and `-` (no spaces). Requires Pillow: `pip3 install pillow`.

## 5. Releasing

- **Version and build number.** iOS: `VERSION` and `BUILD_NUMBER` in `CMakeLists.txt`. Android: `versionName` and `versionCode` in `platforms/android/app/build.gradle`. Both stores reject an upload whose build number isn't higher than the last one.
- **Android signing.** Create `platforms/android/keystore.properties` (gitignored) containing `storeFile`, `storePassword`, `keyAlias` and `keyPassword` (the store file path is relative to `platforms/android/`), then `./gradlew bundleRelease`. Never commit keystores; the `.gitignore` blocks `*.jks`, `*.keystore` and `keystore.properties`.
- **iOS signing.** Set your Team in Xcode, then Product → Archive.
- **Icons.** iOS: replace `platforms/ios/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png` (1024×1024, **no transparency** — the App Store rejects icons with an alpha channel). Android: replace `platforms/android/app/src/main/res/mipmap-*/ic_launcher.png`.
- **Signing Team that survives regeneration.** The Xcode project is generated into `build/ios`, so set the Team through CMake instead of in Xcode: create `CMakeUserPresets.json` (gitignored) in the game folder:

  ```json
  {"version": 6, "configurePresets": [{"name": "ios-signed", "inherits": "ios",
    "cacheVariables": {"CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM": "ABCDE12345"}}]}
  ```

  then `cmake --preset ios-signed`.

## 6. Writing game code

`src/main.cpp`:

```cpp
#include "scenes/title_scene.hpp"
#include <nova/nova.hpp>

nova::GameConfig nova::configureGame() {
    return {.title = "My Game", .designSize = {720, 1280}, .org = "com.studio", .app = "mygame",
            .clearColor = nova::Color::fromHex(0x141A2EFF), .firstScene = nova::makeScene<TitleScene>};
}
```

A scene with a component and a system:

```cpp
struct Spinner { float speed; };              // components are plain structs

void spin(nova::SystemContext& ctx) {          // systems are plain functions
    for (auto [e, t, s] : ctx.world.view<nova::Transform, Spinner>()) t.rotation += s.speed * ctx.dt;
}

class TitleScene final : public nova::Scene {
public:
    void onEnter() override {
        using namespace nova;
        world().reserve<Transform, Sprite, Spinner>(64);         // no allocations during play
        Entity logo = world().spawn();
        world().add<Transform>(logo, Transform{.pos{360, 640}});
        world().add<Sprite>(logo, Sprite{.region = assets().atlas("sprites").region("logo"), .size{192, 192}});
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

Add new source files to `SOURCES` in `CMakeLists.txt`, and test files to `nova_add_game_tests(...)`.

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
