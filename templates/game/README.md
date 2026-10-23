# @GAME_NAME@

A game built on [Nova](https://github.com/pijush-paldragon/nova-engine). Bundle ID: `@BUNDLE_ID@`.

The engine is found at `@ENGINE_REL_PATH@` (relative to this folder). Override it with `-DNOVA_ENGINE_DIR=<path>` or the `NOVA_ENGINE_DIR` environment variable (CMake), or `-PnovaEngineDir=<path>` (Gradle). Every build compiles the engine's current source, so engine changes reach this game on its next build.

## macOS (iterate here)

```bash
cmake --preset macos-debug && cmake --build --preset macos-debug
```

```bash
build/macos-debug/@GAME_TARGET@.app/Contents/MacOS/@GAME_TARGET@
```

Tests: `ctest --preset macos-debug`. Smoke run: add `--autoplay --frames 600 --fail-on-alloc`.

## iOS

```bash
cmake --preset ios && open build/ios/@GAME_TARGET@.xcodeproj
```

Select the `@GAME_TARGET@` scheme, set your Team under Signing & Capabilities, and Run.

## Android

```bash
cd platforms/android && ./gradlew installDebug
```

With several devices attached, choose one with `ANDROID_SERIAL=<serial>`.

## Releasing

- **Version:** `VERSION` / `BUILD_NUMBER` in `CMakeLists.txt` (iOS) and `versionName` / `versionCode` in `platforms/android/app/build.gradle`. Bump the build number and `versionCode` for every store upload.
- **Android signing:** create `platforms/android/keystore.properties` (gitignored) with `storeFile`, `storePassword`, `keyAlias`, `keyPassword`, then `./gradlew bundleRelease`.
- **iOS signing:** your Team in Xcode; Product → Archive.
