# External Games Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let production games live in their own folders outside the engine repo and build against the live engine source, with a one-command generator.

**Architecture:**
- A new `cmake/Nova.cmake` is the single entry point. A game's top-level CMakeLists includes it, and it adds the third-party libraries and the engine into that game's build tree.
- Engine CMake files stop assuming the engine is the top-level project.
- Android per-game logic moves into an engine-owned `nova-app.gradle` that every game's app module applies.
- Brick Breaker is restructured into the same shape as a generated game and becomes the regression test for that path.

**Tech Stack:** CMake ≥ 3.24, Gradle (AGP 8.7.3, wrapper 8.13), Python 3 stdlib (generator + `unittest`), existing Nova/SDL3 stack.

**Spec:** `docs/superpowers/specs/2026-10-23-external-games-design.md`

## Global Constraints

- Work on branch `feat/external-games`. Commit after every task, with messages ending `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Push only in Task 7.
- **No `CMAKE_SOURCE_DIR` in engine CMake code.** Nothing under `cmake/`, `engine/` or `games/` may use it, except Brick Breaker's top-level detection (`CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR`). Use `NOVA_ENGINE_DIR` or `CMAKE_CURRENT_SOURCE_DIR` instead.
- **Default engine location:** `<game>/../nova-engine`. Overrides, in priority order: `-DNOVA_ENGINE_DIR`, then the `NOVA_ENGINE_DIR` env var (CMake), or the `novaEngineDir` Gradle property (Android).
- **Test options:** `NOVA_BUILD_ENGINE_TESTS` defaults to ON only when the engine root is the top-level source dir and the platform is desktop. `NOVA_BUILD_GAME_TESTS` defaults to ON on desktop. Both are OFF on iOS and Android.
- **Android values:** compileSdk and targetSdk 36, minSdk 24, NDK `28.2.13676358`, CMake `3.30.5`, ABIs `arm64-v8a` and `x86_64`, `-DANDROID_STL=c++_static`, Gradle wrapper 8.13, AGP 8.7.3.
- **Fixed Android names:** namespace `com.gamestudio.nova.host`; activity `com.gamestudio.nova.NovaActivity`.
- **Generator validation:**
  - bundle ID `^[A-Za-z][A-Za-z0-9]*(\.[A-Za-z][A-Za-z0-9_]*)+$`;
  - name 1–30 characters without `"<>&`;
  - target name `^[a-z][a-z0-9_]*$`.
- **Design size:** portrait 720×1280, landscape 1280×720.
- **Physical devices:** never touch them. Android checks use only `ANDROID_SERIAL=emulator-5554`; start the `Pixel_9a` AVD if it isn't running.
- **Folder name:** the engine folder is still `GameStudio/Boilerplate` until Task 8. Use paths relative to the repo root.

## Review Focus

These five failure modes are each pinned by a named check:

1. **Paths with spaces** (e.g. `~/Game Studio/My Game`) must build. Tests: `test_engine_path_with_spaces` (Task 5) and the scratch game in a folder containing a space (Task 6).
2. **A game not beside the engine** (nested, or elsewhere on disk) must get a correct relative engine path. Test: `test_relative_engine_path_nested_and_far` (Task 5).
3. **Building a game must not write into the engine folder.** All build products go to the game's build dir. Check: `git -C <engine> status --porcelain` is empty after the Task 6 builds.
4. **Generator failure halfway, or a non-empty target**, must leave no partial game folder. Tests: `test_refuses_non_empty_target` and `test_rollback_on_unreplaced_placeholder` (Task 5).
5. **Engine not found** must stop with guidance, not a cryptic error. Checks: configure with `-DNOVA_ENGINE_DIR=/nope` prints the "nova-engine not found" message, and Gradle with `-PnovaEngineDir=/nope` prints the same guidance (Task 6).

---

## File Structure

```
cmake/Nova.cmake                          NEW  engine entry point (include guard, defaults, adds third_party + engine)
cmake/NovaThirdParty.cmake                MOD  NOVA_ENGINE_DIR paths, explicit binary dirs
cmake/NovaGame.cmake                      MOD  VERSION/BUILD_NUMBER/IOS_PLIST/IOS_LAUNCH_SCREEN; nova_add_game_tests()
CMakeLists.txt                            MOD  project(Nova) + include(cmake/Nova.cmake) + games/brickbreaker
engine/CMakeLists.txt                     MOD  tests gated on NOVA_BUILD_ENGINE_TESTS
engine/tests/CMakeLists.txt               MOD  NOVA_ENGINE_DIR paths
engine/tests/support/game_test_main.cpp   NEW  doctest main for game test targets
platforms/ios/Info.plist.in               MOD  @NOVA_VERSION@ / @NOVA_BUILD_NUMBER@
platforms/android/nova-app.gradle         NEW  shared app-module config
platforms/android/java/com/gamestudio/nova/NovaActivity.java   MOVED from platforms/android/app/src/main/java/...
platforms/android/{settings.gradle,build.gradle,gradle.properties,gradlew*,gradle/,app/}  MOVED → games/brickbreaker/platforms/android/
games/brickbreaker/CMakeLists.txt         MOD  dual-mode (subdirectory or top-level)
games/brickbreaker/platforms/ios/         NEW  copies of the engine iOS defaults
games/brickbreaker/platforms/android/     MOVED Android project, app/build.gradle applies nova-app.gradle
templates/game/**                         NEW  generator source tree
tools/gen_assets.py                       MOD  --template-out DIR (logo/pixel atlas + tap.wav)
tools/new_game.py                         NEW  generator
tools/test_new_game.py                    NEW  generator tests
docs/NEW_GAME.md, README.md               MOD
```

---

### Task 1: `Nova.cmake` entry point and engine-relative paths

**Files:**
- Create: `cmake/Nova.cmake`
- Modify: `CMakeLists.txt`, `cmake/NovaThirdParty.cmake`, `cmake/NovaGame.cmake` (paths only in this task), `engine/CMakeLists.txt`, `engine/tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `include(<engine>/cmake/Nova.cmake)`, valid after `project()`. Defines the `NOVA_ENGINE_DIR` cache var (realpath), the options `NOVA_SANITIZE`, `NOVA_BUILD_ENGINE_TESTS` and `NOVA_BUILD_GAME_TESTS`, the variable `NOVA_MOBILE`, and the targets `nova` and `nova_tests` (engine tests only).
  - The functions `nova_add_game`, `nova_set_warnings`, and `nova_add_game_tests` (Task 2).
  - Third-party binary dirs: `${CMAKE_BINARY_DIR}/nova-third-party/<lib>`; engine: `${CMAKE_BINARY_DIR}/nova`.
- Root `CMakeLists.txt` becomes: `cmake_minimum_required(3.24)`, `project(Nova C CXX)`, `include(cmake/Nova.cmake)`, and `add_subdirectory(games/brickbreaker)`. The `NOVA_GAME` option is removed.

- [ ] **Step 1: Record the baseline.** Run: `cmake --preset macos-debug && cmake --build --preset macos-debug && ctest --preset macos-debug`. Expected: `100% tests passed out of 83`.
- [ ] **Step 2: Write `cmake/Nova.cmake` and slim the root `CMakeLists.txt`.**
  - Move the language settings, `NOVA_MOBILE`, the sanitizer block and the module includes from the root file into `Nova.cmake`, behind `include_guard(GLOBAL)`.
  - `NOVA_ENGINE_DIR` comes from `get_filename_component(... "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)`.
  - The engine-tests default is ON if `NOT NOVA_MOBILE AND NOVA_ENGINE_DIR STREQUAL <realpath of CMAKE_SOURCE_DIR>`.
  - `enable_testing()` is called if either test option is ON.
- [ ] **Step 3: Replace path assumptions.**
  - `CMAKE_SOURCE_DIR` → `NOVA_ENGINE_DIR` in `NovaThirdParty.cmake`, `NovaGame.cmake` and `engine/tests/CMakeLists.txt`.
  - Each `add_subdirectory(<third_party lib> ...)` gets a binary dir argument.
  - `engine/CMakeLists.txt` gates `add_subdirectory(tests)` on `NOVA_BUILD_ENGINE_TESTS`.
- [ ] **Step 4: Verify.**
  - Run: `grep -rn "CMAKE_SOURCE_DIR" cmake engine games`. Expected: no matches.
  - Run: `rm -rf build/macos-debug && cmake --preset macos-debug && cmake --build --preset macos-debug && ctest --preset macos-debug`. Expected: 83/83. (Brick Breaker's level tests are still compiled into `nova_tests` via its old block until Task 2.)
  - Run: `build/macos-debug/games/brickbreaker/brickbreaker.app/Contents/MacOS/brickbreaker --autoplay --frames 1800 --fail-on-alloc; echo exit=$?`. Expected: `exit=0`. Afterwards delete `~/Library/Application Support/gamestudio/brickbreaker/save.dat`.
- [ ] **Step 5: Commit.** `build: add cmake/Nova.cmake entry point; engine paths no longer assume top-level`

---

### Task 2: Game test helper and dual-mode Brick Breaker

**Files:**
- Create: `engine/tests/support/game_test_main.cpp`
- Modify: `cmake/NovaGame.cmake` (add `nova_add_game_tests`), `games/brickbreaker/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's `Nova.cmake` and its options.
- Produces:
  - `nova_add_game_tests(<name> SOURCES <files...>)`. When `NOVA_BUILD_GAME_TESTS` is OFF it does nothing. Otherwise it creates the `<name>_tests` executable from SOURCES plus `${NOVA_ENGINE_DIR}/engine/tests/support/game_test_main.cpp`, with these settings:
    - include dirs: `${CMAKE_CURRENT_SOURCE_DIR}/src`;
    - links: `nova`, `doctest::doctest`, `SDL3::SDL3`;
    - compile options: `-fno-rtti`;
    - definition: `NOVA_GAME_TEST_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets/"`;
    - test registration: `doctest_discover_tests(<name>_tests)`.

    `NovaThirdParty` must add doctest whenever either test option is ON.
  - `game_test_main.cpp` does the same as `engine/tests/test_main.cpp`: set the asset root to `NOVA_GAME_TEST_ASSET_ROOT`, use the dummy video/audio hints, call `setAssertBreakEnabled(false)`, and install the counting operator new under `NOVA_TRACK_ALLOCS`.
  - `games/brickbreaker/CMakeLists.txt`:

    ```cmake
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)   # top-level (Gradle, or cmake -S games/brickbreaker)
        cmake_minimum_required(VERSION 3.24)
        project(BrickBreaker C CXX)
        set(NOVA_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "nova-engine checkout")
        include("${NOVA_ENGINE_DIR}/cmake/Nova.cmake")
    endif()
    nova_add_game(brickbreaker ...unchanged...)
    nova_add_game_tests(brickbreaker SOURCES tests/test_level.cpp src/level.cpp)
    ```

    The old `if(TARGET nova_tests) target_sources(...)` block is removed.

- [ ] **Step 1: Write the failing check.** Run: `test -x build/macos-debug/games/brickbreaker/brickbreaker_tests && echo yes || echo no`. Expected now: `no` (level tests are compiled into `nova_tests`).
- [ ] **Step 2: Implement** the helper, the support main and the dual-mode CMakeLists.
- [ ] **Step 3: Verify the engine root.** Run: `cmake --preset macos-debug && cmake --build --preset macos-debug && ctest --preset macos-debug`. Expected: `100% tests passed out of 83` (`nova_tests` 80 plus `brickbreaker_tests` 3). The Step 1 check now prints `yes`, and `build/macos-debug/engine/tests/nova_tests --list-test-cases | grep -c makeLevel` prints `0`.
- [ ] **Step 4: Verify top-level mode.** Run: `S=<scratchpad>/bb-top; cmake -S games/brickbreaker -B "$S" -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build "$S" && ctest --test-dir "$S"`. Expected: the build succeeds, `3 tests passed`, and there are no `nova_tests` (engine tests OFF because the engine isn't top-level). Then `rm -rf "$S"`.
- [ ] **Step 5: Commit.** `build: nova_add_game_tests and dual-mode brickbreaker CMake project`

---

### Task 3: Per-game iOS identity (version, build number, plist overrides)

**Files:**
- Modify: `cmake/NovaGame.cmake`, `platforms/ios/Info.plist.in`
- Create: `games/brickbreaker/platforms/ios/Info.plist.in`, `games/brickbreaker/platforms/ios/LaunchScreen.storyboard` (copies of the engine defaults)

**Interfaces:**
- Produces new `nova_add_game` arguments: `VERSION` (default `1.0.0`), `BUILD_NUMBER` (default `1`), `IOS_PLIST`, `IOS_LAUNCH_SCREEN`.
  - Resolution order for the plist and launch screen: the explicit argument, then `${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios/<file>`, then `${NOVA_ENGINE_DIR}/platforms/ios/<file>`.
  - The plist placeholders gain `@NOVA_VERSION@` and `@NOVA_BUILD_NUMBER@` (replacing the literal `1.0` and `1`).
  - On Apple platforms, also set `MACOSX_BUNDLE_SHORT_VERSION_STRING` and `MACOSX_BUNDLE_BUNDLE_VERSION` from them.
- Brick Breaker passes `VERSION 1.0.0 BUILD_NUMBER 1`.

- [ ] **Step 1: Failing check.** Temporarily set `BUILD_NUMBER 7` in Brick Breaker. Run: `cmake --preset ios >/dev/null && plutil -extract CFBundleVersion raw build/ios/games/brickbreaker/brickbreaker-Info.plist`. Expected now: CMake errors on the unknown argument, or prints `1`; either way not `7`.
- [ ] **Step 2: Implement** the arguments, the resolution order, the plist placeholders, and the copies in `games/brickbreaker/platforms/ios/`.
- [ ] **Step 3: Verify.**
  - Same command → `7`. Also: `plutil -extract CFBundleShortVersionString raw ...` → `1.0.0`; `plutil -lint ...` → `OK`.
  - Revert to `BUILD_NUMBER 1`, re-run → `1`.
  - Run: `xcodebuild -project build/ios/Nova.xcodeproj -scheme brickbreaker -configuration Debug -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' -derivedDataPath build/ios/DerivedData CODE_SIGNING_ALLOWED=NO build | tail -1`. Expected: `** BUILD SUCCEEDED **`.
- [ ] **Step 4: Commit.** `build(ios): per-game version/build number and plist/launch-screen overrides`

---

### Task 4: Shared Android build logic; Brick Breaker's Android project moves

**Files:**
- Create: `platforms/android/nova-app.gradle`
- Move (`git mv`): `platforms/android/app/src/main/java/com/gamestudio/nova/NovaActivity.java` → `platforms/android/java/com/gamestudio/nova/NovaActivity.java`. Move the rest of `platforms/android/{settings.gradle,build.gradle,gradle.properties,gradlew,gradlew.bat,gradle/,app/}` → `games/brickbreaker/platforms/android/`.
- Modify: `games/brickbreaker/platforms/android/app/build.gradle` (applies the shared script), `games/brickbreaker/platforms/android/gradle.properties` (drop `novaGame`; keep `novaRenderDriver=` and `android.suppressUnsupportedCompileSdk=36`), `.gitignore` (Android patterns become `**/platforms/android/.gradle/` and so on, so they match under `games/*`).

**Interfaces:**
- **Consumes:** the dual-mode Brick Breaker CMakeLists (Task 2).
- **Produces:** `nova-app.gradle`, applied from a game's `app/build.gradle`.
  - **It expects:**
    - `ext.novaGameRoot`: the game repo root, an absolute path;
    - `ext.novaEngineDir`: an absolute path.
  - **It configures `android { ... }`:**
    - SDK/NDK/ABI values from Global Constraints; `externalNativeBuild.cmake { path "${novaGameRoot}/CMakeLists.txt"; version '3.30.5'; buildStagingDirectory "${novaGameRoot}/build/android-cxx" }`;
    - `defaultConfig.externalNativeBuild.cmake.arguments`: `-DANDROID_STL=c++_static`, `-DANDROID_PLATFORM=android-24`, `-DNOVA_ENGINE_DIR=${novaEngineDir}`;
    - `sourceSets.main.java.srcDirs += [SDL java, "${novaEngineDir}/platforms/android/java"]`, `assets.srcDirs = ["${novaGameRoot}/assets"]`;
    - `manifestPlaceholders += [novaRenderDriver: findProperty('novaRenderDriver') ?: '']`;
    - Java 17; `lint.abortOnError false`; `namespace 'com.gamestudio.nova.host'`.
- **Brick Breaker's `app/build.gradle`:**
  - It sets `ext.novaGameRoot = file('../../..').canonicalPath` (= `games/brickbreaker`) and `ext.novaEngineDir = file(findProperty('novaEngineDir') ?: "${novaGameRoot}/../..").canonicalPath`.
  - It checks that `${novaEngineDir}/platforms/android/nova-app.gradle` exists; if not, it throws `GradleException("nova-engine not found at '<path>'. Clone it there, or pass -PnovaEngineDir=<path> (gradle.properties: novaEngineDir=<path>).")`.
  - Then it runs `apply from:` and sets `applicationId 'com.gamestudio.brickbreaker'`, `versionCode 1`, `versionName '1.0.0'`, `resValue app_name 'Brick Breaker'` and `manifestPlaceholders += [novaOrientation: 'sensorPortrait']`.
  - Release signing reads `keystore.properties` if present: `storeFile`, `storePassword`, `keyAlias`, `keyPassword`.

- [ ] **Step 1: Failing check.** Run: `test -d games/brickbreaker/platforms/android && echo moved || echo missing`. Expected now: `missing`.
- [ ] **Step 2: Move and implement.** Delete stale build output in the old location first: `rm -rf platforms/android/.gradle platforms/android/app/build platforms/android/build build/android-cxx` (all gitignored). Copy `local.properties` along with the move (it's gitignored).
- [ ] **Step 3: Verify.**
  - Run: `cd games/brickbreaker/platforms/android && ANDROID_HOME=~/Library/Android/sdk JAVA_HOME=$(/usr/libexec/java_home -v 17) ./gradlew assembleDebug --console=plain | tail -2`. Expected: `BUILD SUCCESSFUL`.
  - Run: `unzip -l app/build/outputs/apk/debug/app-debug.apk | grep -cE "lib/(arm64-v8a|x86_64)/lib(SDL3|main).so|assets/sprites.png"`. Expected: `5`.
  - Run (emulator only): `ANDROID_SERIAL=emulator-5554 adb install -r app/build/outputs/apk/debug/app-debug.apk && adb shell monkey -p com.gamestudio.brickbreaker -c android.intent.category.LAUNCHER 1 && sleep 5 && adb logcat -d | grep -E "Renderer backend"`. Expected: `Renderer backend: vulkan`.
  - Run: `./gradlew help -PnovaEngineDir=/nope 2>&1 | grep -c "nova-engine not found"`. Expected: `≥1`.
- [ ] **Step 4: Commit.** `build(android): shared nova-app.gradle; brickbreaker Android project moves under the game`

---

### Task 5: Game template, template assets and the generator

**Files:**
- Create: `templates/game/**`, `tools/new_game.py`, `tools/test_new_game.py`
- Modify: `tools/gen_assets.py` (`--template-out DIR`)

**Interfaces:**
- Consumes: the shape of the Brick Breaker platform projects (Tasks 3–4); `nova_add_game` and `nova_add_game_tests`.
- Produces:
  - **`gen_assets.py --template-out DIR`:** writes `DIR/sprites.png` (128×128 atlas holding `logo`, a 96×96 rounded square with a ring, and `pixel` at 4×4) plus `DIR/sprites.atlas` and `DIR/sfx/tap.wav` (60 ms sine blip, 44.1 kHz mono 16-bit).
  - **Template tree:** `templates/game/` holds the files listed in spec §7. Rendered names: `CMakeLists.txt.template` → `CMakeLists.txt`, `gitignore.template` → `.gitignore`. The fonts are copied from `games/brickbreaker/assets/fonts/`. The Android Gradle files are copied from Brick Breaker's Android project, with:
    - `novaGameRoot = file('../../..')`;
    - the engine default `"${novaGameRoot}/@ENGINE_REL_PATH@"`;
    - `applicationId '@BUNDLE_ID@'`;
    - `app_name '@GAME_NAME@'`;
    - `novaOrientation: '@ANDROID_ORIENTATION@'`.
  - **Template CMakeLists:** uses the engine lookup block from spec §3 verbatim, then `project(@GAME_TARGET@ C CXX)` (placed *before* the lookup), then `nova_add_game(@GAME_TARGET@ SOURCES src/main.cpp src/scenes/title_scene.cpp ASSETS assets BUNDLE_ID @BUNDLE_ID@ DISPLAY_NAME "@GAME_NAME@" ORIENTATION @ORIENTATION@ VERSION 1.0.0 BUILD_NUMBER 1)`, then `nova_add_game_tests(@GAME_TARGET@ SOURCES tests/test_game.cpp)`.
  - **Template `main.cpp`:** `designSize {@DESIGN_W@, @DESIGN_H@}`, `orientation Orientation::@ORIENTATION@`, `app "@GAME_TARGET@"`.
  - **`TitleScene`:**
    - It centres a 192×192 `logo` sprite in the design area, adds a title label (Fredoka, 72 px) with the game name and a "Tap anywhere" label.
    - On `input().tapped()` it calls `audio().play(tap)`, adds `Tween::scale({1,1},{1.2f,1.2f},0.1f,Ease::OutQuad).pingPong(1)` to the logo, and logs `Tapped`.
    - With `options().autoplay` it simulates a tap every 0.5 s through `timers().every`, so smoke runs exercise the path.
  - **Template test:** `TEST_CASE("game builds and links") { CHECK(nova::Vec2{3,4}.length() == doctest::Approx(5)); }`.
  - **Template `.gitignore`:** the engine's rules (Android patterns written as `platforms/android/...`), plus `platforms/android/keystore.properties`.
  - **`new_game.py`:**
    - Exposes `def generate(target: Path, name: str, bundle_id: str, orientation: str = "portrait", target_name: str | None = None, engine_dir: Path = ENGINE_ROOT, git: bool = True) -> Path`. It raises `GeneratorError(str)` on invalid input; `main()` maps that to exit 2 plus a message.
    - Placeholders are spec §7's set.
    - Text extensions: `.txt .cmake .json .md .cpp .hpp .h .gradle .properties .xml .plist .in .storyboard .java .atlas .template .bat` plus files named `gradlew`. Everything else is copied as binary.
    - Rolls back (`shutil.rmtree`) on any failure after it has created the target.
    - `ENGINE_ROOT` = the parent of `tools/`.

- [ ] **Step 1: Write the failing tests** in `tools/test_new_game.py` (`unittest`; each test generates into `tempfile.TemporaryDirectory()` with `git=False` unless stated):

```python
def test_renders_all_placeholders(self):        # walk output: no r"@[A-Z_]+@" in any text file; CMakeLists has project(mygame
def test_landscape_swaps_design_size(self):     # main.cpp contains "{1280, 720}"; AndroidManifest/app build.gradle has sensorLandscape
def test_relative_engine_path_sibling(self):    # engine tmp/nova-engine, target tmp/MyGame → CMakeLists contains "../nova-engine"
def test_relative_engine_path_nested_and_far(self):  # target tmp/a/b/Game → "../../../nova-engine"; engine elsewhere → correct relpath
def test_engine_path_with_spaces(self):         # target "tmp/My Games/Space Game" renders; CMake path string quoted in output
def test_refuses_non_empty_target(self):        # pre-existing file → GeneratorError, file untouched, no other files created
def test_rejects_bad_bundle_id_and_name(self):  # "com", "1abc.x", "com.x y" and name 'A"B' → GeneratorError, target not created
def test_binary_files_identical_and_gradlew_executable(self):  # Fredoka.ttf bytes == source; os.access(gradlew, X_OK)
def test_rollback_on_unreplaced_placeholder(self):  # templates override with a file containing "@BOGUS@" → GeneratorError, target absent
def test_no_git_flag(self):                     # git=False → no .git; git=True (if git available) → .git and one commit
```

`generate()` takes an optional `template_dir: Path` so the rollback test can point at a temp copy with a bogus marker.

- [ ] **Step 2: Run, expect failure.** Run: `python3 -m unittest tools/test_new_game.py -v`. Expected: `ModuleNotFoundError: No module named 'new_game'`, or an import error.
- [ ] **Step 3: Implement** `--template-out`, generate the template assets into `templates/game/assets`, write the template tree, and write `new_game.py`.
- [ ] **Step 4: Run.** `python3 -m unittest tools/test_new_game.py -v` → `OK` (10 tests). Also run `python3 tools/gen_assets.py --check`, which still passes for Brick Breaker's assets.
- [ ] **Step 5: Commit.** `feat(tools): game template and new_game.py generator`

---

### Task 6: External game end-to-end verification

**Files:** none committed (scratch only). Fix anything found in the files owned by Tasks 1–5, and commit those fixes in this task.

**Interfaces:**
- Consumes: everything above.
- Scratch layout:
  - Engine: the repo itself.
  - Game: `<scratchpad>/Game Studio/Smoke Test` (note the spaces). It's generated with `--name "Smoke Test" --bundle-id com.novatest.smoketest`, giving the target `smoketest`. The game isn't beside the engine, so the generator must compute a non-default relative engine path; nothing is passed explicitly, so this exercises that computation.

- [ ] **Step 1: Generate.** Run: `python3 tools/new_game.py "<scratch>/Game Studio/Smoke Test" --name "Smoke Test" --bundle-id com.novatest.smoketest`. Expected: exit 0, the printed next steps, and `git -C "<game>" log --oneline` showing 1 commit.
- [ ] **Step 2: macOS.** From the game dir, run `cmake --preset macos-debug && cmake --build --preset macos-debug && ctest --preset macos-debug`. Expected: `1 tests passed`. Then run `build/macos-debug/smoketest.app/Contents/MacOS/smoketest --autoplay --frames 600 --fail-on-alloc --screenshot <scratch>/smoke.png; echo exit=$?`. Expected: `exit=0`, the log contains `Tapped` and `Leak check: clean`, and the screenshot shows the logo and title.
- [ ] **Step 3: Engine edit propagation.** Add `log::info("nova propagation check");` to `App::init` in the engine, rebuild **the game only** (`cmake --build --preset macos-debug` in the game dir), and run `--frames 10`. Expected: the log line appears. Then revert the engine edit (`git checkout engine/src/app/app.cpp`) and rebuild.
- [ ] **Step 4: No engine pollution.** Run: `git -C <engine> status --porcelain`. Expected: empty.
- [ ] **Step 5: Engine not found.** Run: `cmake -S "<game>" -B <scratch>/nf -DNOVA_ENGINE_DIR=/nope 2>&1 | grep -c "nova-engine not found"`. Expected: `1`. Also run Gradle `help -PnovaEngineDir=/nope` in the game's `platforms/android`. Expected: the guidance message.
- [ ] **Step 6: iOS.** Run: `cmake --preset ios && xcodebuild -project build/ios/smoketest.xcodeproj -scheme smoketest -configuration Debug -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' -derivedDataPath build/ios/DerivedData CODE_SIGNING_ALLOWED=NO build | tail -1`. Expected: `** BUILD SUCCEEDED **`. Then launch it on the booted iPhone 15 Pro simulator (simulator tool `launch`) and take a screenshot. Expected: the title scene renders, and `plutil` shows `CFBundleIdentifier com.novatest.smoketest`.
- [ ] **Step 7: Android.** Run `./gradlew assembleDebug` (no engine override) in `<game>/platforms/android`, then install on `emulator-5554` only, launch, take a screenshot, and run `adb shell input tap 540 1200`. Expected: logcat shows `Renderer backend: vulkan` and `Tapped`.
- [ ] **Step 8: Brick Breaker regression.** Run engine `ctest --preset macos-debug` (all pass) and the 4200-frame smoke test (`exit=0`). Brick Breaker's iOS simulator build and Android `assembleDebug` already passed in Tasks 3–4; re-run them only if Task 6 changed shared files.
- [ ] **Step 9: Clean up and commit.** Delete the scratch game, then uninstall it from the emulator and the simulator (`adb uninstall com.novatest.smoketest`; `xcrun simctl uninstall booted com.novatest.smoketest`). Commit any fixes: `fix: issues found by external-game end-to-end run`. If there were none, record that.

---

### Task 7: Docs, merge, push

**Files:**
- Modify: `docs/NEW_GAME.md` (rewrite), `README.md`

- [ ] **Step 1: Write the docs.**
  - **`NEW_GAME.md`:**
    - generator usage and options;
    - the layout diagram (`GameStudio/nova-engine` + sibling games);
    - finding and overriding the engine (CMake `-D` / env, Gradle property);
    - the build commands for each platform from inside a game;
    - "all games use the latest engine: after changing the engine, rebuild and smoke-test your games";
    - versioning (`VERSION`/`BUILD_NUMBER` in CMakeLists; `versionCode`/`versionName` in `app/build.gradle`, both bumped for every store upload);
    - release signing via `platforms/android/keystore.properties` (gitignored) and the Xcode Team;
    - the engine rules section, kept from the current document.
  - **`README.md`:** the layout, Brick Breaker's Android path (`games/brickbreaker/platforms/android`), the generator one-liner, and the `NEW_GAME.md` link.
- [ ] **Step 2: Verify links and commands.** Run every command quoted in the docs that wasn't already run in Task 6, or confirm it was. Check with a short script that every repo-relative path mentioned in `README.md` and `NEW_GAME.md` (anything under `cmake/`, `tools/`, `templates/`, `platforms/` or `games/`) exists. Expected: no missing paths.
- [ ] **Step 3: Commit, merge and push.** Commit with `docs: external game workflow`. Then run `git checkout main && git merge --ff-only feat/external-games && ctest --preset macos-debug && git push origin main && git branch -d feat/external-games`. Expected: the tests pass and the push succeeds.

---

### Task 8: Rename `Boilerplate` → `nova-engine` and fresh-build check

**Files:** none (folder rename).

- [ ] **Step 1: Pre-flight.** Run: `git status --porcelain` → empty; `git log origin/main..main` → empty (everything is pushed).
- [ ] **Step 2: Rename.** Run `cd ~/Desktop/GameStudio && mv Boilerplate nova-engine`, then `rm -rf nova-engine/build nova-engine/games/brickbreaker/build nova-engine/games/brickbreaker/platforms/android/.gradle nova-engine/games/brickbreaker/platforms/android/app/build` (stale absolute paths).
- [ ] **Step 3: Fresh build under the new path.**
  - Run: `cd ~/Desktop/GameStudio/nova-engine && cmake --preset macos-debug && cmake --build --preset macos-debug && ctest --preset macos-debug`. Expected: all pass.
  - Run: `grep -rl "GameStudio/Boilerplate" build/macos-debug/CMakeCache.txt build/macos-debug/build.ninja`. Expected: no output.
  - Run the 1800-frame smoke test. Expected: `exit=0`.
- [ ] **Step 4: Report.** Tell the user the new path and that the next session should be opened on `GameStudio/nova-engine`. The git remote and GitHub repo are unchanged.
