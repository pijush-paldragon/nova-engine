# Nova — 2D mobile engine on SDL3

Nova is a small, reusable C++20 engine for 2D games on **iOS** and **Android**, with a **macOS** desktop build for fast iteration. `games/brickbreaker` is a complete sample game built on it.

- **Rendering:** SDL3's renderer, Metal first on iOS/macOS and Vulkan first on Android, with automatic OpenGL / OpenGL ES fallback. Sprites are sorted and batched per texture, so a frame is typically 1–4 draw calls.
- **Game model:** ECS ([EnTT](https://github.com/skypjack/entt)) behind a thin `nova::World` API, plus scenes, systems, timers and tweens.
- **Built in:** Box2D physics, SDL3_mixer audio, SDL3_ttf text, particles, save data, touch/mouse/keyboard input, safe-area aware layout, debug overlay.
- **Memory:** every object has one owner; textures, sounds, fonts and entities are referenced by generational handles, so a stale reference is a safe null, never a crash. Debug builds fail loudly if a gameplay frame allocates or anything leaks.

## Layout

```
engine/include/nova/   public API (the only headers games may include; start with <nova/nova.hpp>)
engine/src/            engine implementation
engine/tests/          unit tests (doctest)
games/brickbreaker/    sample game: src/, assets/, tests/
platforms/ios/         Info.plist template, launch screen
platforms/android/     Gradle project (uses SDL's Java glue straight from the submodule)
third_party/           SDL, SDL_ttf, SDL_mixer, box2d, entt, doctest (git submodules, pinned tags)
tools/                 asset generator and font fetcher
docs/                  design spec, implementation plan, NEW_GAME.md
```

## Prerequisites

- Xcode (iOS SDK) and CMake ≥ 3.24 with Ninja: `brew install cmake ninja`
- Android Studio with NDK `28.2.13676358` and CMake `3.30.5` (SDK Manager → SDK Tools), and JDK 17
- Python 3 (only to regenerate the sample assets)

```bash
git clone --recursive <your-repo-url> Boilerplate
```

If you cloned without `--recursive`:

```bash
git submodule update --init && git -C third_party/SDL_ttf submodule update --init external/freetype
```

## macOS (iterate here)

```bash
cmake --preset macos-debug && cmake --build --preset macos-debug
```

```bash
build/macos-debug/games/brickbreaker/brickbreaker.app/Contents/MacOS/brickbreaker
```

Debug builds run with AddressSanitizer + UndefinedBehaviorSanitizer. Use `macos-release` for an optimised build.

Tests (engine + game logic):

```bash
ctest --preset macos-debug
```

Smoke test — 70 s of self-playing game that fails on any leak, sanitizer report, or heap allocation in a steady-state gameplay frame:

```bash
build/macos-debug/games/brickbreaker/brickbreaker.app/Contents/MacOS/brickbreaker --autoplay --frames 4200 --fail-on-alloc
```

## iOS

```bash
cmake --preset ios
```

```bash
open build/ios/Nova.xcodeproj
```

In Xcode select the `brickbreaker` scheme, set your **Team** under *Signing & Capabilities* (first time only), choose a device or simulator, and Run. Command-line simulator build:

```bash
xcodebuild -project build/ios/Nova.xcodeproj -scheme brickbreaker -configuration Debug -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO build
```

## Android

Open `platforms/android` in Android Studio and press Run, or from a terminal (device or emulator attached):

```bash
cd platforms/android && ./gradlew installDebug
```

The game to build, its application id, name and orientation are Gradle properties in `platforms/android/gradle.properties` (`novaGame`, `novaApplicationId`, `novaAppName`, `novaOrientation`). With several devices attached, pick one with `ANDROID_SERIAL=<serial>` (see `adb devices`). To test the OpenGL ES fallback on a device: `./gradlew installDebug -PnovaRenderDriver=opengles2`.

## Debugging switches

| Switch | Effect |
|---|---|
| `F1` / three-finger tap | Toggle the debug overlay (FPS, frame time, backend, draw calls, entities, bodies, allocations/frame) |
| `NOVA_DEBUG_OVERLAY=1` | Start with the overlay visible |
| `NOVA_RENDER_DRIVER=opengl` (or `opengles2`, `vulkan`, `metal`, `software`) | Force a render backend, e.g. to test the fallback (Android: `-PnovaRenderDriver=...`) |
| `NOVA_LOG_INPUT=1` | Log every touch/mouse event (device input debugging) |
| `--autoplay` | Games may drive themselves (Brick Breaker's paddle plays) |
| `--frames N` | Run N frames at a fixed 60 Hz step, then tear down and run the leak check (non-zero exit on leaks) |
| `--fail-on-alloc` | Fail if a steady-state gameplay frame allocates on the game thread |
| `NOVA_ALLOC_TRAP=1` | With `--fail-on-alloc`: break into the debugger at the offending allocation (run under `lldb`) |
| `--screenshot PATH` | With `--frames`: save the last frame as a PNG |

## Assets

The sample's sprites, atlas, sound effects and music loop are generated (Python standard library only); the font is [Fredoka](https://github.com/google/fonts/tree/main/ofl/fredoka) under the SIL Open Font License (`games/brickbreaker/assets/fonts/OFL.txt`).

```bash
python3 tools/gen_assets.py && bash tools/fetch_font.sh && python3 tools/gen_assets.py --check
```

## Upgrading a dependency

Each library is a submodule pinned to a release tag. To upgrade, check out the new tag inside `third_party/<lib>`, rebuild, run the tests and the smoke test, then commit the submodule bump. Because games only see `nova::` APIs, dependency changes stay inside `engine/`.

## Making a new game

See [docs/NEW_GAME.md](docs/NEW_GAME.md).
