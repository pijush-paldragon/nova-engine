# Nova Engine + Brick Breaker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `nova`, a reusable SDL3-based 2D mobile engine (static library), and `brickbreaker`, a sample game that runs on macOS, iOS and Android.

**Architecture:**
- Layered engine modules (`core` → subsystems → `ecs`/`scene` → `app`), with public headers under `engine/include/nova/`.
- SDL3 main callbacks own the frame loop: fixed 60 Hz simulation plus interpolated rendering through `SDL_Renderer` (Metal/Vulkan first, OpenGL ES fallback).
- Games are separate CMake targets that link `nova` and include only `<nova/...>`.

**Tech Stack:** C++20, CMake ≥ 3.24 + Ninja/Xcode, SDL 3.4.18, SDL_ttf 3.2.2, SDL_mixer 3.2.4, Box2D 3.2.0, EnTT 3.16.0, doctest 2.5.3, Gradle (AGP from SDL's template) with JDK 17, Python 3 (stdlib only) for asset generation.

**Spec:** `docs/superpowers/specs/2026-10-23-nova-engine-design.md`

## Global Constraints

- Namespace `nova::`. Engine library target `nova`; test target `nova_tests`; game target name = game folder name.
- C++20. `-fno-exceptions -fno-rtti` for engine and games (tests may enable exceptions). `-Wall -Wextra -Wpedantic -Werror` on engine sources only.
- No raw `new`/`delete`, no owning raw pointers, no `shared_ptr` in engine code. Third-party C objects are owned by RAII wrappers or `HandlePool` entries.
- Submodule tags (exact): SDL `release-3.4.18`, SDL_ttf `release-3.2.2`, SDL_mixer `release-3.2.4`, box2d `v3.2.0`, entt `v3.16.0`, doctest `v2.5.3`.
- Games may include only `<nova/...>`. SDL, SDL_ttf, SDL_mixer and Box2D include dirs are `PRIVATE` to `nova`. EnTT is the one `PUBLIC` include, because the templated `World` API needs it, but game code never names `entt::`.
- `core` uses SDL only for logging (`SDL_Log*`) and time (`SDL_GetTicksNS`). Nothing else in `core` touches a third-party library.
- Render driver order: iOS `"metal,opengles2"`, Android `"vulkan,opengles2"`, macOS `"metal,opengl"`. The `NOVA_RENDER_DRIVER` env var overrides it. Vsync on.
- Hints: `SDL_HINT_TOUCH_MOUSE_EVENTS="0"`, `SDL_HINT_MOUSE_TOUCH_EVENTS="0"`, `SDL_HINT_AUDIO_CATEGORY="ambient"`.
- Design size for brickbreaker: 720×1280, portrait. World/screen coordinates are y-down, with the origin at the top-left of the design area.
- Fixed step 1/60 s, at most 5 steps per frame. `FrameArena` capacity 1 MiB. Physics scale: 1 m = 100 design px. Default gravity `{0, 980}` px/s².
- Text is always drawn above all sprites (v1). Default `Sprite::layer = 0`, `TextLabel::layer = 0`; layers sort ascending within their kind.
- Save file: `SDL_GetPrefPath(org, app)` + `save.dat`. First line `NOVA_SAVE 1`. Written as `save.dat.tmp`, then renamed.
- iOS deployment target 15.0. Android minSdk 24, targetSdk 36, ABIs `arm64-v8a`, `x86_64`. Default bundle ID `com.gamestudio.<game>`.
- Asset paths are relative to the game's `assets/` root (e.g. `"sprites.atlas"`). Assets land at the bundle `Resources` root on Apple platforms and the APK assets root on Android.
- Commit after every task. Messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push.

**Spec clarifications this plan adopts (also applied to the spec):**
- `core` may use SDL for log and time.
- EnTT's include dir is public.
- Text is drawn above sprites.
- The `Assets` facade sits above `render`/`audio`/`text` (the generic `AssetCache` and atlas parser stay low-level).
- Unreferenced assets are purged after each completed scene transition and on low memory. A `replace` with the same scene type therefore reuses already-loaded assets.
- Screen shake is `Camera2D::shake()`, not a tween.

## Review Focus

These five conditions are each pinned by a named test:

1. **Resuming after a long background pause, or one huge frame:** the game must not fast-forward or spiral. `FixedStepClock` clamps the backlog to 5 steps and `reset()` zeroes it. Test: `FixedStepClock clamps huge frame and drops backlog` (Task 10).
2. **Non-16:9 screens (iPad 4:3, 20:9 phones, landscape window resize):** the whole design area must stay visible, and taps must land where they're drawn. Tests: `computeFit covers design area for 4:3, 16:9, 20:9` (Task 5) and `Input maps window coords through the same fit` (Task 6).
3. **An entity destroyed twice, or destroyed during a view loop:** no crash, and the entity stays valid until the end of the frame. Test: `World destroy is deferred, idempotent and safe inside view` (Task 7).
4. **A second finger landing while the first drags, then the first lifting:** the primary pointer stays the first finger and then switches to the remaining one, with no phantom tap. Test: `Input primary pointer survives multi-touch` (Task 6).
5. **Several scene operations in one frame, or `pop()` on the last scene:** operations apply in FIFO order, and popping the last scene is ignored with an error log. Test: `SceneStack applies ops FIFO and refuses to pop last scene` (Task 9).

---

## File Structure

```
CMakeLists.txt                     root: options NOVA_GAME, NOVA_BUILD_TESTS, NOVA_SANITIZE; adds third_party, engine, games
CMakePresets.json                  macos-debug, macos-release, ios (+ test preset macos-debug)
cmake/NovaThirdParty.cmake         configures + add_subdirectory for every submodule
cmake/NovaWarnings.cmake           nova_set_warnings(target)
cmake/NovaGame.cmake               nova_add_game(name SOURCES.. ASSETS dir [BUNDLE_ID id] [DISPLAY_NAME n])
engine/CMakeLists.txt              nova static lib (GLOB_RECURSE src/*.cpp except src/app/entry.cpp)
engine/include/nova/nova.hpp       umbrella header for games
engine/include/nova/core/          types.hpp result.hpp assert.hpp log.hpp math.hpp random.hpp handle.hpp frame_arena.hpp alloc_stats.hpp
engine/include/nova/platform/      file.hpp
engine/include/nova/assets/        asset_cache.hpp atlas.hpp assets.hpp
engine/include/nova/render/        camera.hpp sprite_batch.hpp renderer.hpp types.hpp
engine/include/nova/input/         input.hpp
engine/include/nova/ecs/           world.hpp components.hpp system.hpp render_bridge.hpp physics.hpp tween.hpp particles.hpp
engine/include/nova/scene/         scene.hpp scene_stack.hpp timers.hpp
engine/include/nova/physics/       body_def.hpp physics_world.hpp
engine/include/nova/audio/         audio.hpp
engine/include/nova/text/          text.hpp
engine/include/nova/save/          save_store.hpp
engine/include/nova/app/           game_config.hpp app_options.hpp fixed_step.hpp
engine/src/<module>/*.cpp          implementations; src/app/{app.cpp, app.hpp, debug_overlay.cpp, entry.cpp}
engine/tests/                      test_main.cpp + test_<module>.cpp, CMakeLists.txt (doctest_discover_tests)
games/brickbreaker/                CMakeLists.txt, src/{main.cpp, components.hpp, systems.hpp, systems.cpp, level.hpp, level.cpp, scenes/*.{hpp,cpp}}, assets/
tools/gen_assets.py, tools/fetch_font.sh
platforms/ios/                     Info.plist.in, LaunchScreen.storyboard
platforms/android/                 settings.gradle, build.gradle, gradle.properties, gradlew(+bat), gradle/wrapper/*, app/{build.gradle, src/main/AndroidManifest.xml, src/main/java/com/gamestudio/nova/NovaActivity.java, src/main/res/values/strings.xml}
docs/NEW_GAME.md, README.md
```

`engine/src/app/entry.cpp` holds `#define SDL_MAIN_USE_CALLBACKS` + `<SDL3/SDL_main.h>`, the four `SDL_App*` callbacks and the debug `operator new` counter. `nova_add_game` compiles it *into the game target*, not the static lib, so the linker can't drop `SDL_main`/`main` or the allocator override.

Test commands used throughout:
- Build: `cmake --build --preset macos-debug`
- Run a test group: `ctest --preset macos-debug -R "<pattern>" --output-on-failure`

---

### Task 1: Repository bootstrap and build skeleton

**Files:**
- Create: `.gitmodules` (via `git submodule add`), `CMakeLists.txt`, `CMakePresets.json`, `cmake/NovaThirdParty.cmake`, `cmake/NovaWarnings.cmake`, `engine/CMakeLists.txt`, `engine/include/nova/core/log.hpp`, `engine/src/core/log.cpp`, `engine/tests/CMakeLists.txt`, `engine/tests/test_main.cpp`, `engine/tests/test_smoke.cpp`
- Modify: `.gitignore` (add `/third_party/**/build/`)

**Interfaces:**
- Produces:
  - `nova::log::info/warn/error(const char* fmt, ...)`, printf-style through `SDL_LogMessageV` with category `SDL_LOG_CATEGORY_APPLICATION`.
  - CMake targets `nova`, `nova_tests`, and the third-party targets `SDL3::SDL3`, `SDL3_ttf::SDL3_ttf-static`, `SDL3_mixer::SDL3_mixer-static`, `box2d`, `EnTT::EnTT`, `doctest::doctest`.

- [ ] **Step 1: Add submodules at exact tags**

```bash
git submodule add https://github.com/libsdl-org/SDL.git third_party/SDL && git -C third_party/SDL checkout release-3.4.18
git submodule add https://github.com/libsdl-org/SDL_ttf.git third_party/SDL_ttf && git -C third_party/SDL_ttf checkout release-3.2.2 && git -C third_party/SDL_ttf submodule update --init external/freetype
git submodule add https://github.com/libsdl-org/SDL_mixer.git third_party/SDL_mixer && git -C third_party/SDL_mixer checkout release-3.2.4
git submodule add https://github.com/erincatto/box2d.git third_party/box2d && git -C third_party/box2d checkout v3.2.0
git submodule add https://github.com/skypjack/entt.git third_party/entt && git -C third_party/entt checkout v3.16.0
git submodule add https://github.com/doctest/doctest.git third_party/doctest && git -C third_party/doctest checkout v2.5.3
```

Expected: `git submodule status` lists six entries at those tags.

- [ ] **Step 2: Write `cmake/NovaThirdParty.cmake`**

Set cache options before each `add_subdirectory`:
- **SDL:** `SDL_TEST_LIBRARY OFF`, `SDL_EXAMPLES OFF`. On Android `SDL_SHARED ON` / `SDL_STATIC OFF`; elsewhere `SDL_SHARED OFF` / `SDL_STATIC ON`.
- **SDL_ttf:** `SDLTTF_VENDORED ON`, `SDLTTF_HARFBUZZ OFF`, `SDLTTF_PLUTOSVG OFF`, `SDLTTF_SAMPLES OFF`, `BUILD_SHARED_LIBS OFF`.
- **SDL_mixer:** `SDLMIXER_VENDORED OFF`, `SDLMIXER_WAVE ON`, `SDLMIXER_VORBIS_STB ON`. Every other decoder option from its CMakeLists `OFF`: FLAC*, GME, MOD*, MP3*, MIDI*, OPUS, VORBIS_VORBISFILE, VORBIS_TREMOR, WAVPACK, AIFF, VOC, AU. Also `SDLMIXER_TESTS OFF`, `SDLMIXER_EXAMPLES OFF`.
- **box2d:** `BOX2D_SAMPLES OFF`, `BOX2D_BENCHMARKS OFF`, `BOX2D_DOCS OFF`, `BOX2D_UNIT_TESTS OFF`.
- **entt, doctest:** `add_subdirectory` with their tests off.

- [ ] **Step 3: Write root `CMakeLists.txt`, `CMakePresets.json`, `cmake/NovaWarnings.cmake`, `engine/CMakeLists.txt`**

- **Root:**
  - `cmake_minimum_required(VERSION 3.24)`; `project(Nova C CXX)`, plus `OBJC` on Apple.
  - Options: `NOVA_GAME` (default `brickbreaker`), `NOVA_BUILD_TESTS` (default ON only when not iOS/Android), `NOVA_SANITIZE` (ASan+UBSan when ON).
  - `add_subdirectory(engine)`, then `games/${NOVA_GAME}` if that folder exists.
- **Engine:**
  - `nova` target: `add_library(nova STATIC)` with `GLOB_RECURSE CONFIGURE_DEPENDS src/*.cpp`, excluding `src/app/entry.cpp`.
  - `PUBLIC` include `include/`; `PRIVATE` `src/`.
  - Links: `PUBLIC EnTT::EnTT`; `PRIVATE` SDL3, SDL3_ttf, SDL3_mixer, box2d.
  - Flags: `cxx_std_20`, `-fno-exceptions -fno-rtti` as PUBLIC compile options; `nova_set_warnings(nova)`.
- **Presets:**
  - `macos-debug`: Ninja, `build/macos-debug`, Debug, `NOVA_SANITIZE=ON`.
  - `macos-release`: Ninja, `build/macos-release`, Release.
  - `ios`: `Xcode` generator, `build/ios`, `CMAKE_SYSTEM_NAME=iOS`, `CMAKE_OSX_DEPLOYMENT_TARGET=15.0`, `CMAKE_OSX_ARCHITECTURES=arm64`.
  - Matching `buildPresets`, plus `testPresets` `macos-debug`.

- [ ] **Step 4: Write the failing smoke test**

`engine/tests/test_main.cpp`: `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + include. `engine/tests/test_smoke.cpp`:

```cpp
TEST_CASE("Smoke: log functions are callable") {
    nova::log::info("hello %d", 42);
    CHECK(true);
}
```

`engine/tests/CMakeLists.txt`: `nova_tests` executable (GLOB `test_*.cpp`), links `nova doctest::doctest SDL3::SDL3`, exceptions enabled, `include(doctest)` from `third_party/doctest/scripts/cmake`, `doctest_discover_tests(nova_tests)`.

- [ ] **Step 5: Configure, build, run**

Run: `cmake --preset macos-debug && cmake --build --preset macos-debug && ctest --preset macos-debug --output-on-failure`
Expected: configure succeeds. The build fails at link time until `log.cpp` exists. Implement `engine/src/core/log.cpp`, rebuild, and get `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "build: add submodules, CMake skeleton, nova lib and test runner"
```

---

### Task 2: Core basics — types, Result, asserts, math, random

**Files:**
- Create: `engine/include/nova/core/{types,result,assert,math,random}.hpp`, `engine/src/core/assert.cpp`
- Test: `engine/tests/test_core.cpp`

**Interfaces:**
- Produces:
  - `types.hpp`: `u8 u16 u32 u64 i32 i64 f32 f64` aliases.
  - `result.hpp`:
    - `struct Error { std::string message; };`
    - `template<class T> class Result`, with `static Result fail(std::string msg)`, implicit construction from `T`, `bool ok() const`, `explicit operator bool`, `T& value()`, `const Error& error() const`.
    - `Result<void>`, with `static Result<void> success()`.
  - `assert.hpp`:
    - `NOVA_ASSERT(cond, msg)`: debug only. Logs file/line/msg, then `SDL_TriggerBreakpoint()`.
    - `NOVA_VERIFY(cond, msg)`: always evaluates `cond`, logs on failure, breaks in debug, and the expression yields `bool`.
  - `math.hpp`:
    - `struct Vec2 { f32 x, y; }` with `+ - * /` (scalar and component-wise), `dot`, `length`, `normalized` (zero-safe), `lerp(Vec2, Vec2, f32)`.
    - `struct Rect { f32 x, y, w, h; bool contains(Vec2) const; bool intersects(const Rect&) const; Vec2 center() const; }`.
    - `struct Color { u8 r, g, b, a; static constexpr Color fromHex(u32 rgba); Color withAlpha(u8) const; }`, plus `lerp(Color, Color, f32)` and constants `Color::White`, `Black`, `Magenta`.
    - `struct Affine2 { f32 a, b, c, d, tx, ty; static Affine2 trs(Vec2 pos, f32 radians, Vec2 scale); Vec2 apply(Vec2) const; }`.
    - `constexpr f32 kPi`.
  - `random.hpp`: `class Random { explicit Random(u64 seed); u32 next(); f32 range(f32 lo, f32 hi); i32 rangeInt(i32 lo, i32 hiInclusive); };` (xorshift64*).

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("Result carries value or error") {
    nova::Result<int> ok = 5;              CHECK(ok.ok());  CHECK(ok.value() == 5);
    auto bad = nova::Result<int>::fail("nope"); CHECK(!bad); CHECK(bad.error().message == "nope");
    CHECK(nova::Result<void>::success().ok());
}
TEST_CASE("Vec2 math") {
    CHECK(nova::Vec2{3,4}.length() == doctest::Approx(5));
    CHECK(nova::Vec2{0,0}.normalized().x == 0);                       // no NaN
    CHECK(nova::lerp(nova::Vec2{0,0}, nova::Vec2{10,20}, 0.5f).y == doctest::Approx(10));
}
TEST_CASE("Rect contains/intersects") {
    nova::Rect r{0,0,10,10};
    CHECK(r.contains({5,5})); CHECK(!r.contains({11,5}));
    CHECK(r.intersects({9,9,5,5})); CHECK(!r.intersects({10.5f,0,2,2}));
}
TEST_CASE("Affine2 rotates 90 degrees about origin then translates") {
    auto m = nova::Affine2::trs({100,0}, nova::kPi/2, {1,1});
    auto p = m.apply({1,0});  CHECK(p.x == doctest::Approx(100)); CHECK(p.y == doctest::Approx(1));
}
TEST_CASE("Color fromHex and Random determinism") {
    constexpr auto c = nova::Color::fromHex(0xFF8000C0); CHECK(c.r == 255); CHECK(c.g == 128); CHECK(c.a == 192);
    nova::Random a{7}, b{7}; CHECK(a.next() == b.next());
    for (int i = 0; i < 1000; ++i) { f32 v = a.range(-1, 1); CHECK(v >= -1); CHECK(v < 1); }
}
```

(The tests use `using namespace nova::` aliases for `f32` where needed.)

- [ ] **Step 2: Run, expect a compile failure** (headers missing): `cmake --build --preset macos-debug`
- [ ] **Step 3: Implement the headers and `assert.cpp`.** y-down convention: a positive angle rotates +x toward +y.
- [ ] **Step 4: Run** `ctest --preset macos-debug -R "Result|Vec2|Rect|Affine2|Color" --output-on-failure`. Expected: all pass.
- [ ] **Step 5: Commit** `feat(core): Result, asserts, math, random`

---

### Task 3: Core memory — handles, frame arena, allocation stats

**Files:**
- Create: `engine/include/nova/core/{handle,frame_arena,alloc_stats}.hpp`, `engine/src/core/{frame_arena,alloc_stats}.cpp`
- Test: `engine/tests/test_memory.cpp`

**Interfaces:**
- Produces:
  - `template<class Tag> struct Handle { u32 index = 0; u32 generation = 0; bool valid() const { return generation != 0; } bool operator==(const Handle&) const = default; };`
  - `template<class T, class Tag = T> class HandlePool`:
    - `Handle<Tag> insert(T&& v)`, `T* get(Handle<Tag>)`, `const T* get(Handle<Tag>) const`, `bool remove(Handle<Tag>)` (destroys `T`), `u32 size() const`, `template<class F> void forEach(F&& f)` (passes `(Handle<Tag>, T&)`), `void clear()`.
    - Slots are reused through a free list. The generation increments on remove and skips 0 on wrap.
  - `class FrameArena`:
    - `explicit FrameArena(size_t capacity)`, `void* allocate(size_t bytes, size_t align = alignof(std::max_align_t))`, `template<class T> std::span<T> allocArray(size_t n)` (value-initialised, T must be trivially destructible), `void reset()`, `size_t used() const`, `size_t capacity() const`, `size_t overflowBytesLastFrame() const`.
    - **Overflow:** when the primary block is full, allocate from heap overflow blocks and log a warning once per frame. `reset()` frees the overflow blocks and grows the primary block to `capacity + overflow` (rounded up to 64 KiB).
  - `alloc_stats.hpp`: `namespace nova::memory { u64 allocationCount(); void recordAllocation(); }`. `recordAllocation` is called by the debug `operator new` in `entry.cpp` (Task 10); a relaxed `std::atomic<u64>`.

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("HandlePool detects stale handles and reuses slots") {
    struct Tex { int id; };
    nova::HandlePool<Tex> pool;
    auto h1 = pool.insert({1});
    REQUIRE(pool.get(h1)); CHECK(pool.get(h1)->id == 1);
    CHECK(pool.remove(h1)); CHECK(pool.get(h1) == nullptr); CHECK(!pool.remove(h1));
    auto h2 = pool.insert({2});
    CHECK(h2.index == h1.index); CHECK(h2.generation != h1.generation);
    CHECK(pool.get(h1) == nullptr); CHECK(pool.get(h2)->id == 2);
    CHECK(pool.get(nova::Handle<Tex>{}) == nullptr);
}
TEST_CASE("HandlePool generation never becomes 0 on wrap") {      // debugSetGeneration exists only under NOVA_TESTING
    nova::HandlePool<int> pool; auto h = pool.insert(1); pool.debugSetGeneration(h.index, UINT32_MAX);
    pool.remove(nova::Handle<int>{h.index, UINT32_MAX}); auto h2 = pool.insert(2);
    CHECK(h2.index == h.index); CHECK(h2.generation == 1);
}
TEST_CASE("FrameArena aligns, resets, and survives overflow") {
    nova::FrameArena arena{256};
    auto* p = arena.allocate(3, 1); auto* q = arena.allocate(8, 16);
    CHECK(reinterpret_cast<uintptr_t>(q) % 16 == 0); CHECK(p != q);
    auto big = arena.allocArray<float>(1000);               // > capacity → overflow, still usable
    CHECK(big.size() == 1000); big[999] = 1.f;
    CHECK(arena.overflowBytesLastFrame() > 0);
    arena.reset(); CHECK(arena.used() == 0); CHECK(arena.capacity() >= 256 + 4000);
}
```

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement.** `HandlePool` stores `std::vector<Slot{std::optional<T> value; u32 generation = 1;}>` plus `std::vector<u32> freeList`. Add a `#ifdef NOVA_TESTING` hook, and define `NOVA_TESTING` for `nova` when `NOVA_BUILD_TESTS` is ON.
- [ ] **Step 4: Run** `ctest --preset macos-debug -R "HandlePool|FrameArena"`. Expected: pass, with no ASan reports.
- [ ] **Step 5: Commit** `feat(core): generational handles, frame arena, alloc stats`

---

### Task 4: SaveStore

**Files:**
- Create: `engine/include/nova/save/save_store.hpp`, `engine/src/save/save_store.cpp`
- Test: `engine/tests/test_save.cpp`

**Interfaces:**
- Produces: `class SaveStore`:
  - Lifecycle: `explicit SaveStore(std::string directory)`, `bool load()` (false when missing or corrupt; the store is empty afterwards), `Result<void> flush()` (writes only when dirty), `bool dirty() const`.
  - Setters: `void setInt(std::string_view key, i64)`, `setFloat(.., f64)`, `setBool(.., bool)`, `setString(.., std::string_view)`.
  - Getters: `i64 getInt(std::string_view key, i64 fallback = 0) const`, `getFloat`, `getBool`, `std::string getString(.., std::string_view fallback = {})`. A getter returns the fallback when the key is missing or holds another type.
  - Keys must match `[A-Za-z0-9_.-]+`. An invalid key makes `NOVA_VERIFY` fail and the setter does nothing.
- File format:
  - Line 1: `NOVA_SAVE 1`.
  - Then one line per key: `i <key> <int>`, `f <key> <%.17g>`, `b <key> 0|1`, or `s <key> <escaped>`.
  - String escaping: `\\` → `\\\\`, newline → `\n`, CR → `\r`.
- I/O: `SDL_IOFromFile` / `SDL_RenamePath`.

- [ ] **Step 1: Write failing tests** (they use a temp dir under `SDL_GetPrefPath("nova-tests","save")`, cleaned at start)

```cpp
TEST_CASE("SaveStore round-trips all types including tricky strings") {
    { nova::SaveStore s{dir}; s.setInt("best_score", 12345); s.setFloat("vol", 0.25); s.setBool("muted", true);
      s.setString("name", "a b\nc\\d"); REQUIRE(s.flush().ok()); CHECK(!s.dirty()); }
    nova::SaveStore r{dir}; REQUIRE(r.load());
    CHECK(r.getInt("best_score") == 12345); CHECK(r.getFloat("vol") == 0.25); CHECK(r.getBool("muted"));
    CHECK(r.getString("name") == "a b\nc\\d");
    CHECK(r.getInt("vol", -1) == -1);                 // type mismatch → fallback
    CHECK(!fileExists(dir + "save.dat.tmp"));         // atomic write leaves no temp file
}
TEST_CASE("SaveStore recovers from corrupt file") {
    writeFile(dir + "save.dat", "garbage\x01\x02");
    nova::SaveStore s{dir}; CHECK(!s.load()); CHECK(s.getInt("x", 7) == 7);
    s.setInt("x", 1); CHECK(s.flush().ok());
}
TEST_CASE("SaveStore flush is a no-op when clean") { nova::SaveStore s{dir}; CHECK(s.flush().ok()); CHECK(!fileExists(dir + "save.dat")); }
```

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement.** Store the values in a `std::map<std::string, Value, std::less<>>` with `using Value = std::variant<i64, f64, bool, std::string>;` (`std::variant` without RTTI is fine). Skip malformed lines with a warning, but a bad header means the file is treated as corrupt.
- [ ] **Step 4: Run** `ctest --preset macos-debug -R SaveStore`. Expected: pass.
- [ ] **Step 5: Commit** `feat(save): atomic key-value SaveStore`

---

### Task 5: Asset foundations, generated assets, and render module

**Files:**
- Create: `engine/include/nova/platform/file.hpp`, `engine/src/platform/file.cpp`, `engine/include/nova/assets/{asset_cache,atlas}.hpp`, `engine/src/assets/atlas.cpp`, `engine/include/nova/render/{types,camera,sprite_batch,renderer}.hpp`, `engine/src/render/{camera,sprite_batch,renderer}.cpp`, `tools/gen_assets.py`, `tools/fetch_font.sh`, `games/brickbreaker/assets/**` (generated, committed)
- Test: `engine/tests/test_assets.cpp`, `engine/tests/test_render.cpp`

**Interfaces:**
- Consumes: `Handle`, `HandlePool`, `FrameArena`, `Result`, math (Tasks 2–3).
- Produces:
  - `file.hpp`: `std::string assetPath(std::string_view rel)` (`SDL_GetBasePath()` + rel on Apple/desktop; `rel` unchanged on Android), `Result<std::vector<u8>> readFile(std::string_view rel)`, `Result<std::string> readTextFile(std::string_view rel)`.
  - `asset_cache.hpp`:
    - `using ScopeId = u32; constexpr ScopeId kGlobalScope = 0;`
    - `template<class T, class Tag> class AssetCache`:
      - Construction: `explicit AssetCache(std::function<std::optional<T>(std::string_view path)> loader, std::function<void(T&)> unloader)`.
      - Use: `Handle<Tag> acquire(std::string_view key, ScopeId scope)` (a failed load returns an invalid handle and caches the failure, so it isn't retried every frame), `T* get(Handle<Tag>)`.
      - Lifetime: `void releaseScope(ScopeId)` (drops that scope's references), `void purgeUnreferenced()` (unloads entries with no scopes), `u32 loadedCount() const`, `void clear()`.
  - `atlas.hpp`: `struct AtlasEntry { std::string name; Rect src; };` `Result<std::vector<AtlasEntry>> parseAtlas(std::string_view text)`. The format is one `name x y w h` per line, with `#` comments and blank lines allowed. The first non-comment line is `image <relative png path>`, exposed as `parseAtlasImage(std::string_view) -> Result<std::string>`.
  - `render/types.hpp`: `struct TextureTag; using TextureHandle = Handle<TextureTag>; enum class BlendMode : u8 { Alpha, Additive }; struct SpriteRegion { TextureHandle texture; Rect src; };` A default `src` (w = 0) means the full texture.
  - `camera.hpp`:
    - `struct ScreenFit { f32 scale; Rect visible; Vec2 windowPx; };`
    - `ScreenFit computeFit(Vec2 windowPx, Vec2 design)`: `scale = min(win.x/design.x, win.y/design.y)`; `visible` is the design rect extended symmetrically along the long axis, in design units.
    - `class Camera2D { Vec2 position{}; f32 zoom = 1; void shake(f32 amplitudePx, f32 seconds); void update(f32 dt, Random&); Vec2 shakeOffset() const; Vec2 screenToWorld(Vec2 design) const; Vec2 worldToScreen(Vec2 world) const; };`. Here `position` is the world point at the design-area origin.
  - `sprite_batch.hpp`:
    - `struct SpriteCmd { u64 key; TextureHandle texture; Rect src; Affine2 xform; Vec2 size; Vec2 pivot; Color color; BlendMode blend; };`
    - `u64 makeSortKey(u8 layer, BlendMode, u16 textureIndex, u16 order)`, with bit layout layer<<56 | blend<<48 | tex<<32 | order<<16.
    - `struct FColor { f32 r, g, b, a; };` `struct Vertex { Vec2 pos; FColor color; Vec2 uv; };`, layout-identical to `SDL_Vertex`. `static_assert` the size and offsets in `renderer.cpp`, then pass the span with `reinterpret_cast`. `Color` is converted to `FColor` (÷255) in `build`.
    - `struct BatchRun { TextureHandle texture; BlendMode blend; u32 firstIndex; u32 indexCount; };`
    - `class SpriteBatch { void begin(); void push(const SpriteCmd&); void build(std::function<Vec2(TextureHandle)> textureSize); std::span<const Vertex> vertices() const; std::span<const i32> indices() const; std::span<const BatchRun> runs() const; };`. The storage is reused vectors reserved to 4096 sprites, and nothing reallocates in steady state.
  - `renderer.hpp` (public part):
    - `class Renderer2D { const char* backendName() const; Rect visibleRect() const; Rect safeArea() const; u32 drawCallsLastFrame() const; void setClearColor(Color); f32 pixelScale() const; Vec2 textureSize(TextureHandle) const; };`
    - The internal API is declared in `engine/src/render/renderer_internal.hpp`:
      - `static Result<std::unique_ptr<Renderer2D>> create(SDL_Window*, Vec2 design)`, using the driver list from Global Constraints plus the `NOVA_RENDER_DRIVER` override, and logging the chosen driver.
      - `void onResize()`, `beginFrame()`, `submit(SpriteBatch&)`, `endFrame()`, `SDL_Renderer* sdl()`.
      - `std::optional<TextureEntry> loadTexture(std::string_view path)` (uses `SDL_LoadPNG_IO` + `SDL_CreateTextureFromSurface`, linear filtering), `TextureHandle checkerboard()` (a 2×2 magenta/black texture with nearest filtering).
      - Rendering applies `scale` and the visible-rect offset so the design origin maps correctly. Texture handles inside the renderer come from a `HandlePool<SDL_Texture owner>`.

- [ ] **Step 1: Generate assets.** Write `tools/gen_assets.py` (stdlib: `zlib`, `struct`, `wave`, `math`, `random` with seed 1234).
  - `sprites.png` is a 256×256 atlas. It holds `paddle` 160×32 (rounded rect), `ball` 32×32 (circle), `brick` 80×36, `particle` 16×16 (soft radial), `pixel` 4×4 (white), all white/greyscale so they can be tinted. Each region gets 2 px of padding.
  - `sprites.atlas` lists the regions after the line `image sprites.png`.
  - `sfx/hit.wav`, `sfx/break.wav` and `sfx/lose.wav` are 44.1 kHz mono 16-bit, 50–400 ms: square/sine blips plus a noise burst.
  - `music/loop.wav` is 22.05 kHz mono 16-bit, about 16 s, a seamless 4-bar arpeggio.
  - The script supports `--out <dir>` (default `games/brickbreaker/assets`) and `--check`, which validates that all files exist and parse and exits 0.

  `tools/fetch_font.sh` curls `https://raw.githubusercontent.com/google/fonts/main/ofl/fredoka/Fredoka%5Bwdth,wght%5D.ttf` → `fonts/Fredoka.ttf` and `OFL.txt` → `fonts/OFL.txt`.

  Run: `python3 tools/gen_assets.py && bash tools/fetch_font.sh && python3 tools/gen_assets.py --check`. Expected: exits 0 and prints the file list.

- [ ] **Step 2: Write failing tests**

```cpp
TEST_CASE("AssetCache dedups, scopes, and purges") {
    int loads = 0, unloads = 0;
    nova::AssetCache<int, struct T> cache{[&](std::string_view p) -> std::optional<int> { ++loads; return p == "missing" ? std::nullopt : std::optional{1}; },
                                          [&](int&) { ++unloads; }};
    auto a = cache.acquire("a.png", 1), a2 = cache.acquire("a.png", 2);
    CHECK(a == a2); CHECK(loads == 1);
    cache.releaseScope(1); cache.purgeUnreferenced(); CHECK(unloads == 0);    // scope 2 still holds it
    cache.releaseScope(2); CHECK(cache.get(a) != nullptr);                     // cached until purge
    cache.purgeUnreferenced(); CHECK(unloads == 1); CHECK(cache.get(a) == nullptr);
    CHECK(!cache.acquire("missing", 1).valid()); CHECK(!cache.acquire("missing", 1).valid()); CHECK(loads == 2); // failure cached
    auto g = cache.acquire("g.png", nova::kGlobalScope); cache.releaseScope(1); cache.purgeUnreferenced(); CHECK(cache.get(g));
}
TEST_CASE("parseAtlas reads regions and rejects malformed lines") {
    auto r = nova::parseAtlas("image sprites.png\n# c\npaddle 2 2 160 32\n\nball 164 2 32 32\n");
    REQUIRE(r.ok()); CHECK(r.value().size() == 2); CHECK(r.value()[1].src.w == 32);
    CHECK(!nova::parseAtlas("image s.png\npaddle 2 2 x 32\n").ok());
    CHECK(nova::parseAtlasImage("image sprites.png\n").value() == "sprites.png");
}
TEST_CASE("computeFit covers design area for 4:3, 16:9, 20:9") {
    for (auto win : {nova::Vec2{1536,2048}, nova::Vec2{1080,1920}, nova::Vec2{1080,2400}, nova::Vec2{2400,1080}}) {
        auto f = nova::computeFit(win, {720,1280});
        CHECK(f.visible.x <= 0.001f); CHECK(f.visible.y <= 0.001f);
        CHECK(f.visible.x + f.visible.w >= 719.999f); CHECK(f.visible.y + f.visible.h >= 1279.999f);
        CHECK(f.visible.w * f.scale == doctest::Approx(win.x)); CHECK(f.visible.h * f.scale == doctest::Approx(win.y));
    }
}
TEST_CASE("SpriteBatch merges same texture runs and orders by layer") {
    nova::SpriteBatch b; b.begin();
    nova::TextureHandle t1{0,1}, t2{1,1};
    auto cmd = [&](u8 layer, nova::TextureHandle t, u16 order) { return nova::SpriteCmd{nova::makeSortKey(layer, nova::BlendMode::Alpha, (u16)t.index, order), t,
                  {0,0,0,0}, nova::Affine2::trs({0,0},0,{1,1}), {10,10}, {0.5f,0.5f}, nova::Color::White, nova::BlendMode::Alpha}; };
    b.push(cmd(1, t1, 0)); b.push(cmd(0, t2, 1)); b.push(cmd(1, t1, 2)); b.push(cmd(1, t2, 3));
    b.build([](nova::TextureHandle) { return nova::Vec2{64,64}; });
    REQUIRE(b.runs().size() == 3);                         // [L0 t2] [L1 t1 ×2] [L1 t2]
    CHECK(b.runs()[0].texture == t2); CHECK(b.runs()[1].indexCount == 12);
    CHECK(b.vertices().size() == 16); CHECK(b.vertices()[0].pos.x == doctest::Approx(-5));   // pivot centred
}
```

- [ ] **Step 3: Run, expect a compile failure.**
- [ ] **Step 4: Implement** `file.cpp`, `AssetCache` (an entry holds `std::optional<T>`, a `std::vector<ScopeId> scopes` and `bool failed`; a `std::unordered_map<std::string, Handle>` maps keys to handles; storage is a `HandlePool`), `atlas.cpp`, `camera.cpp`, `sprite_batch.cpp` (`std::sort` by key, then quad expansion through `xform.apply`; UVs are `src / textureSize`), and `renderer.cpp`.
  - The `Renderer2D` destructor destroys all textures, then the `SDL_Renderer`.
  - `submit` sets the blend mode per run (`SDL_BLENDMODE_BLEND` or `SDL_BLENDMODE_ADD`), calls `SDL_RenderGeometry` once per run, and counts the draw calls.
- [ ] **Step 5: Run** `ctest --preset macos-debug -R "AssetCache|parseAtlas|computeFit|SpriteBatch"`. Expected: pass.
- [ ] **Step 6: Commit** `feat(render,assets): asset cache, atlas, camera fit, sprite batching, generated assets`

---

### Task 6: Input

**Files:**
- Create: `engine/include/nova/input/input.hpp`, `engine/src/input/input.cpp`, `engine/src/input/input_internal.hpp`
- Test: `engine/tests/test_input.cpp`

**Interfaces:**
- Consumes: `ScreenFit` (Task 5).
- Produces:
  - Types: `struct Pointer { u64 id; Vec2 pos; Vec2 delta; bool pressed; bool held; bool released; f32 heldTime; Vec2 startPos; };` `enum class Key : u8 { Left, Right, Up, Down, Space, Enter, Escape, P, R, F1, Count };`
  - `class Input` (public): `int pointerCount() const`, `const Pointer* pointer(int i) const`, `const Pointer* primary() const`, `bool tapped() const`, `bool keyDown(Key) const`, `bool keyPressed(Key) const`, `bool backPressed() const`, `bool threeFingerTap() const`.
  - Internal (`input_internal.hpp`): `void beginFrame(f32 dt)` (clears pressed/released/delta and removes released pointers; call it *before* polling the frame's events), `void handleEvent(const SDL_Event&, const ScreenFit&)`.
  - Coordinate mapping: touch positions are `event.tfinger.x * fit.windowPx.x`; mouse positions are window px × pixel density. Both become design coords via `fit.visible.x + px / fit.scale`.
  - Behaviour:
    - The mouse is pointer id `UINT64_MAX`.
    - A tap is a primary release with `heldTime < 0.3s` and `|pos - startPos| < 20` design px.
    - The primary pointer is the oldest pointer still down.
    - Back is `SDL_SCANCODE_AC_BACK`, plus Escape on desktop.

- [ ] **Step 1: Write failing tests** (build `SDL_Event` structs by hand, with no `SDL_Init` needed)

```cpp
TEST_CASE("Input press/hold/release lifecycle") {
    auto fit = nova::computeFit({720,1280}, {720,1280}); nova::Input in;
    in.beginFrame(0.016f); in.handleEvent(fingerDown(1, 0.5f, 0.5f), fit);
    REQUIRE(in.primary()); CHECK(in.primary()->pressed); CHECK(in.primary()->pos.x == doctest::Approx(360));
    in.beginFrame(0.016f); CHECK(!in.primary()->pressed); CHECK(in.primary()->held);
    in.handleEvent(fingerUp(1, 0.5f, 0.5f), fit); CHECK(in.primary()->released); CHECK(in.tapped());
    in.beginFrame(0.016f); CHECK(in.pointerCount() == 0);
}
TEST_CASE("Input drag is not a tap") { /* down at 0.5, motion to 0.6 (72px), up → !tapped(), delta.x ≈ 72 on motion frame */ }
TEST_CASE("Input primary pointer survives multi-touch") {
    // finger 1 down; next frame finger 2 down → primary()->id == 1; finger 1 up (after a 72px drag) → !tapped();
    // next frame primary()->id == 2 and !primary()->pressed (no phantom press)
}
TEST_CASE("Input maps window coords through the same fit") {
    auto fit = nova::computeFit({1536,2048}, {720,1280});          // 4:3 tablet, wider than design
    nova::Input in; in.beginFrame(0.016f); in.handleEvent(fingerDown(1, 0.0f, 0.5f), fit);
    CHECK(in.primary()->pos.x == doctest::Approx(fit.visible.x));   // left screen edge = visible rect left (< 0)
    CHECK(in.primary()->pos.y == doctest::Approx(640));
}
TEST_CASE("Input keys and back") { /* key down Space → keyPressed & keyDown; next frame keyDown only; AC_BACK → backPressed */ }
```

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement.** Pointers are stored in a `std::array<Pointer, 10>` plus a count. Keys are two `std::bitset<(size_t)Key::Count>`. Three-finger tap: three pointers pressed within the same 0.15 s window.
- [ ] **Step 4: Run** `ctest --preset macos-debug -R Input`. Expected: pass.
- [ ] **Step 5: Commit** `feat(input): unified touch/mouse pointers and keys`

---

### Task 7: ECS World, components, render bridge

**Files:**
- Create: `engine/include/nova/ecs/{world,components,system,render_bridge}.hpp`, `engine/src/ecs/{world,render_bridge}.cpp`
- Test: `engine/tests/test_ecs.cpp`

**Interfaces:**
- Consumes: `SpriteBatch`, `SpriteRegion`, `BlendMode`, `Camera2D`, `FrameArena`.
- Produces:
  - `enum class Entity : u32 {};` `constexpr Entity kNullEntity = static_cast<Entity>(entt::null);`
  - `components.hpp`:
    - `struct Transform { Vec2 pos{}; f32 rotation = 0; Vec2 scale{1,1}; Vec2 prevPos{}; f32 prevRotation = 0; };`
    - `struct Sprite { SpriteRegion region; Vec2 size{}; Vec2 pivot{0.5f,0.5f}; Color color = Color::White; u8 layer = 0; BlendMode blend = BlendMode::Alpha; bool visible = true; };`
    - `struct Name { std::string value; };`
  - `class World`:
    - Entities: `Entity spawn()`, `void destroy(Entity)` (deferred and idempotent), `bool alive(Entity) const` (true until the flush), `bool pendingDestroy(Entity) const`, `void flushDestroyed()`, `size_t count() const`.
    - Components: `template<class C, class... A> C& add(Entity, A&&...)` (emplace_or_replace; sets `prevPos = pos` when adding a `Transform`), `template<class C> C& get(Entity)`, `template<class C> C* tryGet(Entity)`, `template<class C> bool has(Entity) const`, `template<class C> void remove(Entity)`.
    - Queries: `template<class... C> auto view()` (iterable yielding `[Entity, C&...]` from `registry.view<C...>().each()`).
    - Hooks: `template<class C> void onDestroy(void(*fn)(void* user, World&, Entity), void* user)`, an internal hook over entt `on_destroy<C>`.
    - Systems: `void addSystem(Phase, SystemFn)`, `void runSystems(Phase, SystemContext&)`.
    - Internal helpers: `void snapshotTransforms()` (copies prev = current; called before each fixed step), `entt::basic_registry<Entity>& registry()` (internal).
  - `system.hpp`:
    - `enum class Phase : u8 { Fixed, Update };`
    - `struct SystemContext { World& world; Input& input; Physics& physics; Assets& assets; Camera2D& camera; FrameArena& frame; Random& random; f32 dt; };` (forward-declared types). Task 12 appends `Audio& audio`; every reference is bound to a live object.
    - `using SystemFn = void(*)(SystemContext&);`
  - `render_bridge.hpp`: `void collectSprites(World&, SpriteBatch&, f32 alpha, Vec2 cameraOffset)`. It interpolates `lerp(prevPos, pos, alpha)` and the rotation the same way, skips invisible sprites and those pending destruction, and uses `makeSortKey(layer, blend, tex.index, order++)`.

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("World destroy is deferred, idempotent and safe inside view") {
    nova::World w; std::vector<nova::Entity> es;
    for (int i = 0; i < 5; ++i) { auto e = w.spawn(); w.add<nova::Transform>(e); es.push_back(e); }
    int visited = 0;
    for (auto [e, t] : w.view<nova::Transform>()) { w.destroy(es[0]); w.destroy(es[0]); w.destroy(e); ++visited; }
    CHECK(visited == 5); CHECK(w.alive(es[0])); CHECK(w.pendingDestroy(es[0]));
    w.flushDestroyed(); CHECK(w.count() == 0); CHECK(!w.alive(es[0]));
    w.destroy(es[0]); w.flushDestroyed();                     // destroying a dead entity is a no-op
}
TEST_CASE("World onDestroy hook fires once per removed component") { /* hook counts; destroy entity with C → 1; remove<C> on another → 2 */ }
TEST_CASE("collectSprites interpolates between previous and current transform") {
    nova::World w; auto e = w.spawn(); auto& t = w.add<nova::Transform>(e, nova::Transform{.pos{0,0}});
    w.add<nova::Sprite>(e, nova::Sprite{.region{{0,1}, {0,0,10,10}}, .size{10,10}});
    w.snapshotTransforms(); t.pos = {100, 0};
    nova::SpriteBatch b; b.begin(); nova::collectSprites(w, b, 0.5f, {0,0}); b.build([](auto) { return nova::Vec2{10,10}; });
    CHECK(b.vertices()[0].pos.x == doctest::Approx(50 - 5));
}
TEST_CASE("Systems run in registration order per phase") { /* two capture-less lambdas appending to a static vector; Fixed vs Update separated */ }
```

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement.** Pending destruction is a `std::vector<Entity>` plus an entt storage tag `struct PendingDestroy{}` (keeps `pendingDestroy` O(1)). Systems are stored in two `std::vector<SystemFn>`.
- [ ] **Step 4: Run** `ctest --preset macos-debug -R "World|collectSprites|Systems"`. Expected: pass.
- [ ] **Step 5: Commit** `feat(ecs): World wrapper, core components, render bridge`

---

### Task 8: Physics (Box2D) and ECS bridge

**Files:**
- Create: `engine/include/nova/physics/{body_def,physics_world}.hpp`, `engine/src/physics/physics_world.cpp`, `engine/include/nova/ecs/physics.hpp`, `engine/src/ecs/physics.cpp`
- Test: `engine/tests/test_physics.cpp`

**Interfaces:**
- Consumes: `World::onDestroy`, `Transform`, `Entity`.
- Produces:
  - `BodyDef`:
    - Constructors: `static BodyDef dynamic()`, `kinematic()`, `staticBody()`.
    - Shape: `BodyDef& box(Vec2 sizePx)`, `circle(f32 radiusPx)`.
    - Material: `density(f32)` (default 1), `friction(f32)` (default 0.6), `restitution(f32)` (default 0).
    - Flags: `bullet()`, `sensor()`, `fixedRotation()`.
    - The fields are public, for the bridge to read.
  - `PhysicsWorld` (ECS-agnostic, `src` private use of box2d):
    - `explicit PhysicsWorld(Vec2 gravityPx = {0, 980})`, `using BodyId = u64;` (b2BodyId bit-packed), `BodyId createBody(const BodyDef&, Vec2 posPx, f32 rot, u32 userData)`, `void destroyBody(BodyId)`, `void step(f32 dt)` (4 substeps).
    - Body state: `Vec2 position(BodyId)`, `f32 rotation(BodyId)`, `Vec2 velocity(BodyId)`, `setVelocity(BodyId, Vec2)`, `setTransform(BodyId, Vec2, f32)`, `setTargetPosition(BodyId, Vec2 px, f32 dt)` (via `b2Body_SetTargetTransform`), `applyImpulse(BodyId, Vec2)`.
    - Events: `struct RawContact { u32 userA, userB; Vec2 normal; bool sensor; }; std::span<const RawContact> contacts() const` (rebuilt each step from begin-touch and sensor-begin events; normal from `b2Contact_GetData` when `b2Contact_IsValid`, otherwise {0,0}).
    - Counters and units: `u32 bodyCount() const`, `static constexpr f32 kPixelsPerMeter = 100.f`.
  - Component: `struct RigidBody { PhysicsWorld::BodyId id = 0; };`
  - `class Physics` (ECS bridge, game-facing):
    - Construction: `Physics(World&, Vec2 gravity = {0, 980})`.
    - Bodies: `void attach(Entity, const BodyDef&)` (reads the entity's `Transform`, stores the entity id as userData, adds `RigidBody`), `Vec2 velocity(Entity)`, `void setVelocity(Entity, Vec2)`, `void setTargetPosition(Entity, Vec2)`, `void setPosition(Entity, Vec2)`, `void setGravity(Vec2)`.
    - Contacts: `struct Contact { Entity a, b; Vec2 normal; bool sensor; }; std::span<const Contact> contacts() const`, which skips entities pending destruction and accumulates across all fixed steps of the frame. `void beginFrame()` clears it; the App calls `beginFrame()` before the frame's fixed loop, so `Phase::Update` systems see every contact from this frame.
    - Frame calls: `void step(f32 dt)`, `void syncTransforms()` (dynamic and kinematic bodies → `Transform.pos` / `rotation`).
    - Cleanup: registers `world.onDestroy<RigidBody>` → `destroyBody`. `~Physics()` first disconnects that hook, then destroys the `b2World`, so the destruction order of `Physics` and `World` doesn't matter.
    - Access: `PhysicsWorld& physicsWorld()`.

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("Physics dynamic body falls under gravity and syncs Transform") {
    nova::World w; nova::Physics p{w};
    auto e = w.spawn(); w.add<nova::Transform>(e, nova::Transform{.pos{100,100}});
    p.attach(e, nova::BodyDef::dynamic().circle(10));
    for (int i = 0; i < 60; ++i) { w.snapshotTransforms(); p.step(1.f/60); p.syncTransforms(); }
    CHECK(w.get<nova::Transform>(e).pos.y > 500);                 // ~ 100 + 0.5*980*1²
    CHECK(w.get<nova::Transform>(e).pos.x == doctest::Approx(100));
}
TEST_CASE("Physics reports contacts and sensor hits with entities") {
    // zero gravity; ball dynamic at (100,100) velocity (0,500); static box at (100,200) size (200,20); sensor box at (100,400)
    // step ≤ 60 frames, collect contacts → one non-sensor contact {ball, wall} (either order) with |normal.y| ≈ 1
}
TEST_CASE("Physics destroys body when entity is destroyed") {
    nova::World w; nova::Physics p{w}; auto e = w.spawn(); w.add<nova::Transform>(e);
    p.attach(e, nova::BodyDef::staticBody().box({10,10})); CHECK(p.physicsWorld().bodyCount() == 1);
    w.destroy(e); w.flushDestroyed(); CHECK(p.physicsWorld().bodyCount() == 0);
}
TEST_CASE("Physics destroyed before World is safe") {
    nova::World w; { nova::Physics p{w}; auto e = w.spawn(); w.add<nova::Transform>(e); p.attach(e, nova::BodyDef::dynamic().circle(5)); }
    w.flushDestroyed();   // World (and its RigidBody storage) outlives Physics; ASan must stay silent at scope exit
}
TEST_CASE("Physics kinematic target is reached in one step") {
    // kinematic box at (0,0); setTargetPosition(e,{60,0}); step(1/60); syncTransforms → pos.x ≈ 60 (epsilon 0.5)
}
```

(The debug overlay reads the body count through `physicsWorld()` too.)

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement.** Pack `b2BodyId` into a `u64` with `memcpy` (`static_assert(sizeof(b2BodyId) <= 8)`). Every shape created gets `enableContactEvents = true`, and sensors get `enableSensorEvents = true` on both the sensor shape and the visitor shapes.
- [ ] **Step 4: Run** `ctest --preset macos-debug -R Physics`. Expected: pass, with no ASan reports.
- [ ] **Step 5: Commit** `feat(physics): Box2D PhysicsWorld and ECS Physics bridge`

---

### Task 9: Scenes, SceneStack, Timers, Assets facade (textures/atlases)

**Files:**
- Create: `engine/include/nova/scene/{scene,scene_stack,timers}.hpp`, `engine/src/scene/{scene_stack,timers}.cpp`, `engine/include/nova/assets/assets.hpp`, `engine/src/assets/assets.cpp`, `engine/src/scene/services.hpp`
- Test: `engine/tests/test_scene.cpp`

**Interfaces:**
- Consumes: `World`, `Camera2D`, `AssetCache`, `parseAtlas`, `Renderer2D` internals.
- Produces:
  - `services.hpp` (internal): `struct Services { Renderer2D* renderer; Input* input; Audio* audio; Assets* assets; SaveStore* save; Text* text; const AppOptions* options; Random* random; };`. A test can pass nulls for the services it doesn't use.
  - `Timers`: `using TimerId = u32; TimerId after(f32 sec, std::function<void()>); TimerId every(f32 sec, std::function<void()>); void cancel(TimerId); void update(f32 dt); void clear(); size_t active() const;`. Timers added during `update` first fire on the next update. A timer cancelled during `update` doesn't fire.
  - `class Scene`:
    - Lifecycle hooks: `virtual onEnter/onExit/onPause/onResume()`, `virtual void onFixedUpdate(f32)`, `virtual void onUpdate(f32)`.
    - Accessors: `world()`, `physics()`, `timers()`, `camera()`, `assets()`, `input()`, `renderer()`, `save()`, `scenes()`, `options()`, `random()`. `audio()` is added in Task 12 and `text()` in Task 13.
    - Fields: `bool opaque = true;` and `ScopeId scope() const`.
    - Owns `World world_; Physics physics_{world_, {0, 980}}; Timers timers_; Camera2D camera_;`, declared in that order.
  - `class SceneStack`:
    - Construction: `explicit SceneStack(Services&)`.
    - Requests (all queued): `template<class T, class... A> void push(A&&...)`, `template<class T, class... A> void replace(A&&...)`, `void pop()`.
    - Frame calls: `void applyPending()` (runs the queued ops in FIFO order; each scene gets a fresh `ScopeId`, starting at 1, set as `assets.activeScope` around its lifecycle calls; calls `assets.releaseScope(scope)` after `onExit`, then `assets.purgeUnreferenced()` once at the end), `Scene* top()`, `template<class F> void forEachVisible(F&&)` (bottom-up, starting at the topmost opaque scene).
    - Queries and shutdown: `size_t size() const`, `bool empty() const`, `void clear()` (exits every scene top-down; used at shutdown).
    - `pop()` when exactly one scene would remain zero is refused: error log, no-op.
    - Call order: `push` calls the current top's `onPause` and then the new scene's `onEnter`; `pop` calls `onExit` and then the scene below's `onResume`; `replace` calls `onExit` and then the new scene's `onEnter`.
  - `class Assets`:
    - Loading: `TextureHandle texture(std::string_view path, ScopeId scope = kActiveScope)`, `Atlas atlas(std::string_view name, ScopeId scope = kActiveScope)` (loads `<name>.atlas` and its image).
    - Scope control: `void setActiveScope(ScopeId)`, `void releaseScope(ScopeId)`, `void purgeUnreferenced()`.
    - `kActiveScope = UINT32_MAX` is a sentinel for "the scene currently running".
    - Missing texture → `renderer.checkerboard()`.
    - `class Atlas { SpriteRegion region(std::string_view name) const; bool valid() const; }`. A missing region returns the checkerboard plus an error log (once per name).
    - Sounds, music and fonts are added in Tasks 12–13.

- [ ] **Step 1: Write failing tests** (use a `Recorder` scene subclass that appends `"<name>:enter"`, `":exit"`, etc. to a shared log; `Services` with a null renderer, and an `Assets` built over a fake loader through a test constructor `Assets(AssetCache<...> textures)` under `NOVA_TESTING`)

```cpp
TEST_CASE("SceneStack applies ops FIFO and refuses to pop last scene") {
    stack.push<Recorder>("A"); stack.applyPending(); CHECK(log == V{"A:enter"});
    stack.push<Recorder>("B"); stack.replace<Recorder>("C"); stack.applyPending();
    CHECK(log == V{"A:enter","A:pause","B:enter","B:exit","C:enter"}); CHECK(stack.size() == 2);
    stack.pop(); stack.pop(); stack.applyPending();
    CHECK(stack.size() == 1); CHECK(log.back() == "A:resume");        // second pop refused
}
TEST_CASE("SceneStack forEachVisible starts at topmost opaque scene") { /* A opaque, B opaque, C opaque=false → visits B, C */ }
TEST_CASE("SceneStack releases scene-scoped assets on exit, keeps global ones") {
    // A's onEnter acquires "a.png" (active scope) and "g.png" (kGlobalScope); replace A→B → fake unloader called for a.png only
}
TEST_CASE("SceneStack replace with same asset reuses it without reload") { /* A loads x.png; replace A→A2 which loads x.png → loads == 1 */ }
TEST_CASE("Timers after/every/cancel including mutation during update") {
    nova::Timers t; int a = 0, b = 0; nova::TimerId idB = 0;
    t.after(0.5f, [&] { ++a; t.after(0.1f, [&] { ++a; }); t.cancel(idB); });
    idB = t.every(0.2f, [&] { ++b; });
    t.update(0.25f); CHECK(b == 1);  t.update(0.25f); CHECK(a == 1); CHECK(b == 1);   // cancelled within same update before firing
    t.update(0.1f); CHECK(a == 2);   t.clear(); CHECK(t.active() == 0);
}
```

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement.** Pending ops are `struct Op { enum Kind; std::unique_ptr<Scene> scene; }` in a `std::vector`; scenes are constructed at request time, while `onEnter` waits until `applyPending`. `Timers` keeps `std::vector<Timer>` plus a pending-add vector merged after the iteration; cancellation sets `dead = true`.
- [ ] **Step 4: Run** `ctest --preset macos-debug -R "SceneStack|Timers"`. Expected: pass.
- [ ] **Step 5: Commit** `feat(scene): Scene, SceneStack, Timers, Assets facade`

---

### Task 10: App shell, frame loop, lifecycle, debug overlay, brickbreaker stub

**Files:**
- Create: `engine/include/nova/app/{game_config,app_options,fixed_step}.hpp`, `engine/src/app/{fixed_step,app_options,app,debug_overlay}.cpp`, `engine/src/app/{app,debug_overlay}.hpp`, `engine/src/app/entry.cpp`, `engine/include/nova/nova.hpp`, `cmake/NovaGame.cmake`, `platforms/ios/Info.plist.in`, `games/brickbreaker/CMakeLists.txt`, `games/brickbreaker/src/main.cpp`, `games/brickbreaker/src/scenes/menu_scene.{hpp,cpp}` (stub)
- Test: `engine/tests/test_app.cpp`

**Interfaces:**
- Consumes: everything from Tasks 2–9.
- Produces:
  - `game_config.hpp`:
    - `enum class Orientation : u8 { Portrait, Landscape };`
    - `using SceneFactory = std::unique_ptr<Scene>(*)();` and `template<class T> std::unique_ptr<Scene> makeScene() { return std::make_unique<T>(); }`
    - `struct GameConfig { const char* title; Vec2 designSize; Orientation orientation = Orientation::Portrait; const char* org = "gamestudio"; const char* app; Color clearColor = Color::Black; SceneFactory firstScene; };`
    - `GameConfig configureGame();`, declared in `nova` and defined by the game.
  - `app_options.hpp`: `struct AppOptions { bool autoplay = false; i32 maxFrames = -1; bool failOnAlloc = false; };` `AppOptions parseAppOptions(int argc, char** argv)`. Flags: `--autoplay`, `--frames N`, `--fail-on-alloc`; unknown flags are ignored.
  - `fixed_step.hpp`: `class FixedStepClock { explicit FixedStepClock(f64 step = 1.0/60, i32 maxSteps = 5); i32 advance(f64 frameSeconds); f32 alpha() const; void reset(); f64 step() const; };`. When `advance` would exceed `maxSteps`, the leftover backlog is dropped (the accumulator is set to `fmod(acc, step)`).
  - `App` (internal, `app.hpp`):
    - Members: `Result<void> init(int argc, char** argv)`, `SDL_AppResult iterate()`, `SDL_AppResult event(const SDL_Event&)`, `void shutdown()`.
    - `init` does the following in order:
      1. Set the hints.
      2. `SDL_Init(VIDEO | AUDIO)`.
      3. Create the window (high-pixel-density, resizable, fullscreen on mobile, 540×960 window on desktop when portrait).
      4. Create `Renderer2D`.
      5. Set up audio and text, which are no-ops until Tasks 12–13.
      6. Create `Assets`, `SaveStore` (load), `Input`, `SceneStack`.
      7. Push `firstScene`, then `applyPending`.
    - Any failure → `SDL_ShowSimpleMessageBox` + `SDL_APP_FAILURE`.
    - `iterate` follows spec §6:
      1. Reset the frame arena. Input for this frame has already arrived through `SDL_AppEvent`. The App calls `input.beginFrame(dt)` at the *end* of each `iterate`, so the next frame's events land in a fresh snapshot.
      2. `physics.beginFrame()`, then the fixed loop (top scene only): `world.snapshotTransforms()` → `physics.step(dt)` → `physics.syncTransforms()` → `runSystems(Fixed)` → `onFixedUpdate`.
      3. Update: `onUpdate` → `runSystems(Update)` → `timers.update` → `camera.update`.
      4. Render: `collectSprites` for each visible scene (texts are added in Task 13) → overlay → present.
      5. `flushDestroyed` → `applyPending`.
      6. After `maxFrames` frames, return `SDL_APP_SUCCESS`. When the stack is empty, also return `SDL_APP_SUCCESS`.
    - Lifecycle per spec §6. A paused app returns from `iterate` immediately, without rendering or advancing time.
    - `onResize` on `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`.
    - Shutdown destroys in reverse order: scene stack → assets → text → audio → renderer → window → `SDL_Quit`. Then `HandlePool` leak counts are logged, and the app exits non-zero via `SDL_APP_FAILURE` when `maxFrames > 0` and leaks > 0.
    - `--fail-on-alloc`: after frame 120 (warm-up), any frame where `allocationCount()` changes logs an error and the app returns `SDL_APP_FAILURE`.
  - `debug_overlay`: `class FpsCounter { void add(f32 dt); f32 fps() const; f32 avgMs() const; }` (a 60-sample ring). `void drawDebugOverlay(SDL_Renderer*, const FrameStats&)` using `SDL_RenderDebugText`, toggled by `Key::F1` / `threeFingerTap()`.
  - `entry.cpp`: the four `SDL_App*` callbacks forwarding to a function-static `App` (stored in `*appstate`), plus `operator new`/`delete` overrides calling `memory::recordAllocation()` in builds with `NOVA_TRACK_ALLOCS` (defined in Debug).
  - `nova_add_game(name SOURCES ... ASSETS dir [BUNDLE_ID id] [DISPLAY_NAME n])`:
    - **Android:** `add_library(main SHARED ...)` (the name `main` is what `SDLActivity` loads).
    - **Apple:** `add_executable(name MACOSX_BUNDLE ...)`. Each asset file is added as a source with `MACOSX_PACKAGE_LOCATION Resources/<subdir>`; on iOS also set `XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER`, `MACOSX_BUNDLE_INFO_PLIST platforms/ios/Info.plist.in`, `XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"`, and add `LaunchScreen.storyboard` (Task 16).
    - Always: add `engine/src/app/entry.cpp`, and link `nova` + `SDL3::SDL3`.
  - `nova.hpp` includes every public header used by games.

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("FixedStepClock steps at 60Hz and reports alpha") {
    nova::FixedStepClock c; CHECK(c.advance(1.0/60) == 1); CHECK(c.advance(0.5/60) == 0);
    CHECK(c.alpha() == doctest::Approx(0.5f)); CHECK(c.advance(0.5/60) == 1);
}
TEST_CASE("FixedStepClock clamps huge frame and drops backlog") {
    nova::FixedStepClock c; CHECK(c.advance(10.0) == 5); CHECK(c.advance(0.0) == 0); CHECK(c.alpha() < 1.0f);
    c.advance(0.9/60); c.reset(); CHECK(c.alpha() == 0.f); CHECK(c.advance(0.5/60) == 0);
}
TEST_CASE("parseAppOptions") {
    const char* argv[] = {"bb", "--autoplay", "--frames", "1800", "--bogus", "--fail-on-alloc"};
    auto o = nova::parseAppOptions(6, const_cast<char**>(argv));
    CHECK(o.autoplay); CHECK(o.maxFrames == 1800); CHECK(o.failOnAlloc);
}
TEST_CASE("FpsCounter averages") { nova::FpsCounter f; for (int i = 0; i < 60; ++i) f.add(1.f/120); CHECK(f.fps() == doctest::Approx(120).epsilon(0.01)); }
```

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement the engine pieces above.**
- [ ] **Step 4: Write the brickbreaker stub.** `main.cpp` defines `configureGame()` with title `"Brick Breaker"`, `designSize {720,1280}`, `app "brickbreaker"`, `clearColor 0x14182BFF`, `firstScene makeScene<MenuScene>`. The stub `MenuScene::onEnter` spawns one `ball` sprite from `assets().atlas("sprites")` at {360,640}, sized 64×64, and the stub `MenuScene::onUpdate` rotates it. `games/brickbreaker/CMakeLists.txt` is `nova_add_game(brickbreaker SOURCES ... ASSETS assets BUNDLE_ID com.gamestudio.brickbreaker DISPLAY_NAME "Brick Breaker")`.
- [ ] **Step 5: Run the tests, then the app**

Run: `ctest --preset macos-debug -R "FixedStep|parseAppOptions|FpsCounter"`. Expected: pass.
Run: `build/macos-debug/games/brickbreaker/brickbreaker.app/Contents/MacOS/brickbreaker --frames 180 --fail-on-alloc; echo exit=$?`. Expected: the log shows `Renderer backend: metal`, and the output ends with `exit=0`, no ASan output and no leak report. A screenshot of the window shows the ball centred.
Run: `NOVA_RENDER_DRIVER=opengl build/.../brickbreaker --frames 60; echo exit=$?`. Expected: `Renderer backend: opengl`, `exit=0`.

- [ ] **Step 6: Commit** `feat(app): SDL callbacks app shell, frame loop, lifecycle, overlay, brickbreaker stub`

---

### Task 11: Tweens, particles, camera shake

**Files:**
- Create: `engine/include/nova/ecs/{tween,particles}.hpp`, `engine/src/ecs/{tween,particles}.cpp`
- Modify: `engine/src/app/app.cpp` (run `updateTweens` / `updateParticles` after the `Update` systems), `engine/src/ecs/render_bridge.cpp` (draw particles)
- Test: `engine/tests/test_tween.cpp`

**Interfaces:**
- Consumes: `World`, `Transform`, `Sprite`, `Random`, `SpriteBatch`, `SpriteRegion`.
- Produces:
  - Easing: `enum class Ease : u8 { Linear, InQuad, OutQuad, InOutQuad, OutBack, OutElastic, OutBounce };` `f32 ease(Ease, f32 t)` (t is clamped to [0,1]).
  - `enum class TweenProp : u8 { Position, Scale, Rotation, Alpha };` `enum class TweenLoop : u8 { None, Loop, PingPong };`
  - `struct Tween`:
    - Fields: `TweenProp prop; Vec2 from, to; f32 duration; f32 elapsed = 0; Ease easing = Ease::Linear; TweenLoop loop = TweenLoop::None; i32 repeats = 0; bool removeOnComplete = true;`
    - Factories: `static Tween position(Vec2 from, Vec2 to, f32 dur, Ease)`, `scale(...)`, `rotation(f32, f32, f32, Ease)`, `alpha(f32, f32, f32, Ease)`.
    - Modifiers: `Tween& pingPong(i32 times = -1)`, `Tween& looping(i32 times = -1)`.
    - Scalars use `.x`. `repeats = -1` means forever. Alpha is written to `Sprite.color.a`, and to `TextLabel.color.a` once Task 13 exists.
  - `void updateTweens(World&, f32 dt)`.
  - Particles:
    - `struct EmitterConfig { SpriteRegion region; f32 rate = 0; Vec2 lifetime{0.4f,0.8f}; Vec2 speed{100,300}; Vec2 angleDeg{0,360}; Color startColor = Color::White, endColor = Color::White.withAlpha(0); Vec2 size{12,4}; Vec2 gravity{0,0}; u8 layer = 50; BlendMode blend = BlendMode::Additive; };`
    - `struct ParticleEmitter { EmitterConfig config; bool destroyWhenDone = false; void burst(u32 n); u32 alive() const; /* SoA pool */ }`
    - `ParticleEmitter makeEmitter(EmitterConfig, u32 capacity)` allocates the pool up front and caps it at capacity.
    - `void updateParticles(World&, f32 dt, Random&)` destroys emitters with `destroyWhenDone && alive() == 0 && rate == 0`.
    - `void collectParticles(World&, SpriteBatch&, Vec2 cameraOffset)`.

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("ease endpoints and overshoot") {
    for (auto e : {nova::Ease::Linear, nova::Ease::InQuad, nova::Ease::OutQuad, nova::Ease::InOutQuad, nova::Ease::OutBack, nova::Ease::OutElastic, nova::Ease::OutBounce}) {
        CHECK(nova::ease(e, 0.f) == doctest::Approx(0)); CHECK(nova::ease(e, 1.f) == doctest::Approx(1)); }
    CHECK(nova::ease(nova::Ease::OutBack, 0.7f) > 1.f);
}
TEST_CASE("Tween reaches target and removes itself") {
    nova::World w; auto e = w.spawn(); w.add<nova::Transform>(e);
    w.add<nova::Tween>(e, nova::Tween::scale({1,1}, {2,2}, 0.5f, nova::Ease::Linear));
    nova::updateTweens(w, 0.25f); CHECK(w.get<nova::Transform>(e).scale.x == doctest::Approx(1.5f));
    nova::updateTweens(w, 0.3f);  CHECK(w.get<nova::Transform>(e).scale.x == doctest::Approx(2)); CHECK(!w.has<nova::Tween>(e));
}
TEST_CASE("Tween ping-pong once returns to start") { /* scale 1→2, 0.5s, pingPong(1): after 1.0s total scale == 1 and tween removed */ }
TEST_CASE("Particles burst, cap at capacity, die, and self-destroy") {
    nova::World w; nova::Random r{1}; auto e = w.spawn(); w.add<nova::Transform>(e);
    auto& em = w.add<nova::ParticleEmitter>(e, nova::makeEmitter({.lifetime{0.5f,0.5f}}, 32)); em.destroyWhenDone = true;
    em.burst(50); CHECK(em.alive() == 32);
    nova::updateParticles(w, 0.6f, r); w.flushDestroyed(); CHECK(!w.alive(e));
}
TEST_CASE("Camera2D shake decays to zero") { /* shake(10, 0.2); update(0.1) → |offset| ≤ 10; update(0.2) → offset == {0,0} */ }
```

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement.** The particle SoA is one `std::unique_ptr<f32[]>` block, sliced into posX, posY, velX, velY, age and life arrays plus `u32 count`. Particles are spawned in world space at the entity's `Transform.pos`. Each tween's `from` is applied on its first update.
- [ ] **Step 4: Run** `ctest --preset macos-debug -R "ease|Tween|Particles|Camera2D"`. Expected: pass.
- [ ] **Step 5: Commit** `feat(ecs): tweens, particles, camera shake`

---

### Task 12: Audio (SDL3_mixer)

**Files:**
- Create: `engine/include/nova/audio/audio.hpp`, `engine/src/audio/audio.cpp`
- Modify: `engine/include/nova/assets/assets.hpp` + `engine/src/assets/assets.cpp` (add `sound`, `music`), `engine/include/nova/ecs/system.hpp` (append `Audio& audio`), `engine/include/nova/scene/scene.hpp` (add `audio()`), `engine/src/app/app.cpp` (create Audio; pause/resume on lifecycle)
- Test: `engine/tests/test_audio.cpp`

**Interfaces:**
- Consumes: `AssetCache`, `assetPath`, `Assets`.
- Produces:
  - Handles: `struct SoundTag; using SoundHandle = Handle<SoundTag>; struct MusicTag; using MusicHandle = Handle<MusicTag>;`
  - Parameters: `enum class Bus : u8 { Master, Sfx, Music };` `struct PlayParams { f32 volume = 1; f32 pitch = 1; };` `struct MusicParams { bool loop = true; i32 fadeInMs = 0; };`
  - `class Audio`:
    - Playback: `void play(SoundHandle, PlayParams = {})` (a no-op for an invalid handle), `void playMusic(MusicHandle, MusicParams = {})`, `void stopMusic(i32 fadeOutMs = 0)`.
    - Volume: `void setVolume(Bus, f32)` (clamped to [0,1]), `f32 volume(Bus) const`.
    - Lifecycle: `void pauseAll()`, `void resumeAll()`.
    - Loading: `bool available() const` (false if the device couldn't open; everything else then becomes a no-op).
  - Internal:
    - `static std::unique_ptr<Audio> create()`: `MIX_Init` + `MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr)`, 16 sfx tracks plus 1 music track.
    - Loaders: `std::optional<SoundEntry> loadSound(path)` (`MIX_LoadAudio(mixer, assetPath(path), true)`), `loadMusic(path)` (predecode = false).
    - Voice stealing: round-robin over tracks that aren't playing, otherwise the oldest.
    - Gain: per-track gain is `volume * bus(Sfx)`, pitch goes through `MIX_SetTrackFrequencyRatio`, and master uses `MIX_SetMixerGain`.
  - `Assets`: `SoundHandle sound(std::string_view, ScopeId = kActiveScope)`, `MusicHandle music(std::string_view, ScopeId = kActiveScope)`.

- [ ] **Step 1: Write failing tests** (set `SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy")`, `SDL_Init(SDL_INIT_AUDIO)` in a fixture; the asset base is the test's working dir, so point `assetPath` at `games/brickbreaker/assets` through `NOVA_ASSET_ROOT`, a test-only override in `file.cpp` under `NOVA_TESTING`)

```cpp
TEST_CASE("Audio loads generated wav and plays without error") {
    auto audio = nova::Audio::create(); REQUIRE(audio); REQUIRE(audio->available());
    auto s = audio->loadSound("sfx/hit.wav"); REQUIRE(s.has_value());
    // via an AssetCache in the test: play 20 times > 16 tracks → no crash, voice stealing
}
TEST_CASE("Audio missing file yields invalid handle and silent play") { /* Assets-like cache acquire("sfx/nope.wav") → !valid; audio->play(invalid) no crash */ }
TEST_CASE("Audio volume clamps") { audio->setVolume(nova::Bus::Sfx, 3.f); CHECK(audio->volume(nova::Bus::Sfx) == 1.f); }
```

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement**, including the destructor order: tracks → audio → mixer → `MIX_Quit`. Wire it in: App lifecycle calls `pauseAll`/`resumeAll`.
- [ ] **Step 4: Run** `ctest --preset macos-debug -R Audio`. Expected: pass.
- [ ] **Step 5: Commit** `feat(audio): SDL3_mixer sfx/music with buses`

---

### Task 13: Text (SDL3_ttf)

**Files:**
- Create: `engine/include/nova/text/text.hpp`, `engine/src/text/text.cpp`
- Modify: `assets.hpp/.cpp` (add `font`), `engine/include/nova/scene/scene.hpp` (add `text()`), `engine/src/app/app.cpp` (create Text; draw texts after sprites; rescale fonts when the renderer resizes), `engine/src/ecs/tween.cpp` (Alpha → `TextLabel`)
- Test: `engine/tests/test_text.cpp`

**Interfaces:**
- Consumes: `Renderer2D` internals (`sdl()`, `pixelScale()`), `AssetCache`, `World::onDestroy`.
- Produces:
  - Types: `struct FontTag; using FontHandle = Handle<FontTag>;` `enum class TextAlign : u8 { Left, Center, Right };`
  - Component: `struct TextLabel { FontHandle font; Color color = Color::White; TextAlign align = TextAlign::Left; u8 layer = 0; bool visible = true; void set(std::string_view s); const std::string& string() const; u32 revision() const; };`. `set` bumps `revision` only when the content differs.
  - `class Text`:
    - Measuring: `Vec2 measure(FontHandle, std::string_view) const` (design units).
    - Counters: `u32 rebuildsTotal() const`.
  - Internal:
    - `static Result<std::unique_ptr<Text>> create(SDL_Renderer*)` (`TTF_Init` + `TTF_CreateRendererTextEngine`).
    - Fonts: `std::optional<FontEntry> loadFont(std::string_view key)`, where the key is `"path@sizePx"`; opened at `sizePx * pixelScale` (that scale is set by the App each frame).
    - `void setPixelScale(f32)` calls `TTF_SetFontSize` on all fonts when the scale changes and marks every label dirty.
    - `void collectAndDraw(World&, const ScreenFit&, Vec2 camOffset)` keeps a per-entity `TTF_Text*` in a `HandlePool` indexed via a hidden component, `TextCache{Handle}`. It rebuilds via `TTF_SetTextString` only when `revision` changed, sorts by layer, and draws with `TTF_DrawRendererText` at the screen-space position from `Transform` + align.
    - The `onDestroy<TextCache>` hook destroys the `TTF_Text`.
  - `Assets`: `FontHandle font(std::string_view path, f32 sizePx, ScopeId = kActiveScope)`.

- [ ] **Step 1: Write failing tests** (a software renderer: `SDL_CreateSoftwareRenderer(SDL_CreateSurface(720,1280,SDL_PIXELFORMAT_RGBA32))`; font `fonts/Fredoka.ttf`)

```cpp
TEST_CASE("Text measures and rebuilds only on change") {
    // create Text, load font "fonts/Fredoka.ttf@48"; entity with Transform + TextLabel set("Score 0")
    // collectAndDraw → rebuildsTotal()==1; set("Score 0") again → revision unchanged; collectAndDraw → still 1
    // set("Score 10") → collectAndDraw → 2; measure(font,"Score 10").x > 0
}
TEST_CASE("Text missing font is invalid and drawing it is harmless") { /* loadFont("fonts/nope.ttf@32") → nullopt; label with invalid font draws nothing, no crash */ }
TEST_CASE("Text label destroy releases TTF_Text") { /* destroy entity → flush → internal pool size()==0 (exposed under NOVA_TESTING) */ }
```

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement**, including the destructor order: texts → fonts → text engine → `TTF_Quit`.
- [ ] **Step 4: Run** `ctest --preset macos-debug -R Text`, then `brickbreaker --frames 120`. Expected: pass, `exit=0`.
- [ ] **Step 5: Commit** `feat(text): SDL3_ttf labels with change-only rebuilds`

---

### Task 14: Brick Breaker game

**Files:**
- Create: `games/brickbreaker/src/{components.hpp, systems.hpp, systems.cpp, level.hpp, level.cpp}`, `games/brickbreaker/src/scenes/{menu_scene,play_scene,pause_scene,game_over_scene}.{hpp,cpp}`
- Modify: `games/brickbreaker/src/main.cpp`, `games/brickbreaker/CMakeLists.txt`
- Test: `games/brickbreaker/tests/test_level.cpp`. The game's CMakeLists does `if(TARGET nova_tests) target_sources(nova_tests PRIVATE tests/test_level.cpp src/level.cpp) target_include_directories(nova_tests PRIVATE src) endif()`.

**Interfaces:**
- Consumes: the full public API through `#include <nova/nova.hpp>` only.
- Produces (game-internal):
  - Components: `struct Paddle {}; struct Ball { f32 speed = 720; }; struct Brick { i32 hp; }; struct DeathZone {}; struct Hud {};`
  - `LevelLayout makeLevel(i32 level)` with `struct LevelLayout { i32 cols = 8; i32 rows; std::vector<i32> hp; }`. `rows = 4 + level` (level starts at 1), with a maximum of 10. `hp = 1 + (row * 3) / rows` (so 1..3), varying by row from bottom to top.
  - Brick geometry: 80×36 bricks with an 8 px gap, centred horizontally, top row at y = 220.
  - Colours: hp 1 `0x4CC9F0FF`, hp 2 `0xF72585FF`, hp 3 `0xFFD166FF`.
  - Fixed systems (registered in `PlayScene::onEnter`):
    - `paddleControl`: drives the paddle to the primary pointer's x, or to the ball's x when `options().autoplay`. It's clamped to the design width, using `physics.setTargetPosition`.
    - `ballSpeed`: renormalises the ball velocity to `Ball::speed`. If `|vy| < 0.25 * speed`, it sets `vy = ±0.25 * speed` and renormalises.
  - Update systems:
    - `brickHits`: for each non-sensor contact between a `Ball` and a `Brick`: hp−−, a scale ping-pong tween 1→1.15 over 0.08 s, a `hit` sound; when hp reaches 0, a `break` sound, a 24-particle burst emitter (`destroyWhenDone`), score += 10 × level, destroy the brick.
    - Death zone: a sensor contact with `DeathZone` → lives−−, `lose` sound, `camera().shake(12, 0.25f)`, ball respawn above the paddle after 0.6 s through `timers().after`.
- Scenes:
  - **`MenuScene`:** on the first enter, it applies the saved `music_volume` / `sfx_volume` (default 0.6 / 1.0) to `audio().setVolume`. Title "BRICK BREAKER" (Fredoka 72) and "Best: N" (36), with "Tap to play" pulsing through an alpha ping-pong tween. A tap or Space → `scenes().replace<PlayScene>()`. Music starts on the first enter and keeps playing across scenes; music is loaded with `kGlobalScope`.
  - **`PlayScene`:**
    - Gravity `{0,0}`. Walls are static boxes at the visible rect's left, right and top edges. The death zone is a sensor below `visibleRect` bottom − 10.
    - The paddle is kinematic, 160×32, at y = 1140. The ball is dynamic: circle r = 16, bullet, restitution 1, friction 0, launched at (0.35, −1) × speed.
    - The HUD shows "Score", "Lives" and "Level" text placed inside `renderer().safeArea()`.
    - Lives start at 3. No bricks left → level++ and `makeLevel`. Lives 0 → `replace<GameOverScene>(score)`.
    - Background and back button → `push<PauseScene>()`. In `onPause` the scene stops processing; the engine already skips simulation for non-top scenes.
  - **`PauseScene`:** `opaque = false`. A translucent fullscreen `pixel` sprite, `0x000000A0`, and "Paused — tap to resume" → `pop()`. `PlayScene` pushes it from `onBackground()` and when `input().backPressed()`.
  - **`GameOverScene(i32 score)`:** shows the score and, when score > `best_score`, "NEW BEST!" plus `save().setInt("best_score", score); save().flush()`. A tap → `replace<MenuScene>()`.
  - With `--autoplay`: MenuScene auto-starts after 0.5 s, GameOverScene auto-returns after 1 s.
- Engine addition this task requires: `Scene::onBackground()`, called by the App on the top scene for `SDL_EVENT_WILL_ENTER_BACKGROUND`. Add it to `scene.hpp` and to the `App` lifecycle code.

- [ ] **Step 1: Write failing tests** (`games/brickbreaker/tests/test_level.cpp`)

```cpp
TEST_CASE("makeLevel grows rows and caps at 10") {
    CHECK(makeLevel(1).rows == 5); CHECK(makeLevel(6).rows == 10); CHECK(makeLevel(50).rows == 10);
    auto l = makeLevel(3); CHECK(l.hp.size() == size_t(l.cols * l.rows));
    for (int hp : l.hp) { CHECK(hp >= 1); CHECK(hp <= 3); }
}
TEST_CASE("brick layout fits inside design width") { /* brickRect(col,row) for all cells within [0,720] */ }
```

- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement the level, systems and scenes**, plus the `onBackground` engine hook.
- [ ] **Step 4: Run the tests and the smoke run**

Run: `ctest --preset macos-debug -R "makeLevel|brick layout"`. Expected: pass.
Run: `build/macos-debug/games/brickbreaker/brickbreaker.app/Contents/MacOS/brickbreaker --autoplay --frames 1800 --fail-on-alloc; echo exit=$?`. Expected: `exit=0`, no ASan/UBSan output, no "allocation during gameplay" error, and no leak report. The log shows bricks breaking (score > 0 logged at exit).

If `--fail-on-alloc` trips, find the allocation and remove it. Typical causes are `std::string` building in the HUD (switch to `std::to_chars` into a fixed buffer, and `TextLabel::set` compares before assigning) and lambda captures that are too large for `std::function`'s small-buffer optimisation in timers. Don't loosen the check.

- [ ] **Step 5: Manual check.** Launch without flags and take a screenshot of the menu and of play. Check that the paddle follows the mouse, bricks break with particles and sound, and F1 shows the overlay with `metal`.
- [ ] **Step 6: Commit** `feat(brickbreaker): sample game with menu, play, pause, game over`

---

### Task 15: macOS release check and docs

**Files:**
- Create: `README.md`, `docs/NEW_GAME.md`
- Modify: `docs/superpowers/specs/2026-10-23-nova-engine-design.md` (fold in the spec clarifications listed under Global Constraints)

- [ ] **Step 1: Release build and smoke**

Run: `cmake --preset macos-release && cmake --build --preset macos-release && build/macos-release/games/brickbreaker/brickbreaker.app/Contents/MacOS/brickbreaker --autoplay --frames 1800; echo exit=$?`. Expected: builds with `-Werror` clean, `exit=0`.

- [ ] **Step 2: Write docs.**
  - **`README.md`:** prerequisites (Xcode, Android Studio with NDK 28 + CMake 3.30, JDK 17, Python 3), `git clone --recursive`, and the three build flows with the exact commands from Tasks 9, 16 and 17.
  - **`docs/NEW_GAME.md`:** copy the `games/brickbreaker` structure, `nova_add_game`, `-DNOVA_GAME=<name>` / the Gradle property `novaGame=<name>`, and the scene/system/asset patterns, with a 30-line skeleton.
  - **Spec edits:** apply the six clarifications.
- [ ] **Step 3: Commit** `docs: README, new-game guide, spec clarifications`

---

### Task 16: iOS build

**Files:**
- Create: `platforms/ios/LaunchScreen.storyboard`
- Modify: `platforms/ios/Info.plist.in` (orientation keys from `NOVA_ORIENTATION`, `UILaunchStoryboardName=LaunchScreen`, `UIRequiresFullScreen=true`, `UIStatusBarHidden=true`, `CFBundleDisplayName=${NOVA_DISPLAY_NAME}`), `cmake/NovaGame.cmake` (iOS-specific properties from Task 10)

- [ ] **Step 1: Generate and build for the Simulator**

Run: `cmake --preset ios && xcodebuild -project build/ios/Nova.xcodeproj -scheme brickbreaker -configuration Debug -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO build 2>&1 | tail -3`. Expected: `** BUILD SUCCEEDED **`.

- [ ] **Step 2: Run on the Simulator.** Use the iOS Simulator tool: attach first, then launch `build/ios/Debug-iphonesimulator/brickbreaker.app`. Expected: the menu renders full-screen in portrait with no letterbox bars and the HUD below the notch. Tap → play; the paddle drags. The log contains `Renderer backend: metal`. Take a screenshot of the menu and of play.
- [ ] **Step 3: Device build check.** Run: `xcodebuild ... -sdk iphoneos -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO build`. Expected: `** BUILD SUCCEEDED **`. Signing for a physical device is left to the user (Team selection), and the steps are documented in the README.
- [ ] **Step 4: Commit** `build(ios): Xcode project generation, plist, launch screen`

---

### Task 17: Android build

**Files:**
- Create: `platforms/android/{settings.gradle, build.gradle, gradle.properties, gradlew, gradlew.bat, gradle/wrapper/*}` (copied from `third_party/SDL/android-project`, with AGP/Gradle versions kept as SDL pins them), `platforms/android/app/build.gradle`, `app/src/main/AndroidManifest.xml` (adapted from SDL's: `com.gamestudio.nova.NovaActivity`, `android:screenOrientation="sensorPortrait"`, `glEsVersion 0x00020000`, `android.hardware.vulkan.level` with `required="false"`), `app/src/main/java/com/gamestudio/nova/NovaActivity.java`, `app/src/main/res/values/strings.xml`

**Interfaces:**
- Gradle properties: `novaGame=brickbreaker`, `novaApplicationId=com.gamestudio.brickbreaker`, `novaAppName=Brick Breaker`.
- `app/build.gradle`:
  - SDK and ABIs: `namespace "com.gamestudio.nova"`, `minSdk 24`, `targetSdk 36`, `compileSdk 36`, `ndkVersion "28.2.13676358"`, `abiFilters 'arm64-v8a','x86_64'`.
  - Native build: `externalNativeBuild.cmake { path file("../../../CMakeLists.txt"); version "3.30.5" }`, `arguments "-DNOVA_GAME=${novaGame}", "-DANDROID_STL=c++_shared"`.
  - Sources: `sourceSets.main.java.srcDirs += "../../../third_party/SDL/android-project/app/src/main/java"`, `assets.srcDirs = ["../../../games/${novaGame}/assets"]`.
- `NovaActivity extends org.libsdl.app.SDLActivity`, with `getLibraries()` returning `{"SDL3", "main"}`.

- [ ] **Step 1: Build the APK**

Run: `cd platforms/android && ANDROID_HOME=~/Library/Android/sdk JAVA_HOME=$(/usr/libexec/java_home -v 17) ./gradlew assembleDebug 2>&1 | tail -3`. Expected: `BUILD SUCCESSFUL`, and `app/build/outputs/apk/debug/app-debug.apk` exists.

- [ ] **Step 2: Run on the emulator.** List AVDs (`emulator -list-avds`). If none exists, ask the user before creating one, since that downloads a system image. Then `adb install -r` the APK, `adb shell am start -n com.gamestudio.brickbreaker/com.gamestudio.nova.NovaActivity`, and `adb logcat -d | grep -E "Renderer backend|Nova"`. Expected: `Renderer backend: vulkan`, or `opengles2` on emulator images without Vulkan; both are valid and prove the fallback. Take a screenshot with `adb exec-out screencap -p`. Check the menu with no letterbox bars, then tap through to play.
- [ ] **Step 3: Lifecycle check.** `adb shell input keyevent KEYCODE_HOME`, then relaunch. Expected: the PauseScene is shown, and logcat shows the save flushed on background with no crash. Then the back key (`input keyevent 4`) in play → pause.
- [ ] **Step 4: Commit** `build(android): Gradle project using SDL Java glue and root CMake`

---

## Execution notes

- Tasks 1–14 are strictly sequential: each one consumes the previous tasks' interfaces. Tasks 16 and 17 depend only on Task 14 and touch disjoint files, so they can run in parallel. Task 15 can run any time after 14.
- If any pinned third-party API differs from what a step names (signature drift), adapt inside the engine's `src/` and keep the public interfaces above unchanged.
