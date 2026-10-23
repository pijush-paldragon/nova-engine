# Encrypted Asset Pack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Release builds ship one compressed, encrypted, tamper-evident `game.pak` instead of loose asset files, on macOS, iOS and Android.

**Architecture:**
- A shared pack-format module (Monocypher + zstd, std-only types) is compiled into both a host tool (`nova_pack`, built for the build machine at configure time) and the engine.
- CMake stages assets per configuration with a post-build script: loose files in Debug, `game.pak` in Release-type configs. Android's release variant takes the pack from a staging folder.
- The engine opens every asset through one `openAsset()` that reads from the mounted pack when present.

**Tech Stack:** C++20 (engine), C++17 (host tool), Monocypher 4.0.3, zstd v1.5.7, CMake ≥ 3.24, Gradle (AGP 8.7.3), Python 3 `unittest` for tool tests.

**Spec:** `docs/superpowers/specs/2026-10-24-asset-pack-design.md`

## Global Constraints

- **Branch:** `feat/asset-pack`. Commit after every task, with messages ending `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Push only in Task 7.
- **Submodules:** `third_party/monocypher` at tag `4.0.3`, `third_party/zstd` at tag `v1.5.7`. Built from their source files, not their build systems; zstd is built with `ZSTD_DISABLE_ASM`.
- **Pack format v1:**
  - 64-byte header: magic `"NOVAPAK\0"`, u32 version `1`, u32 reserved `0`, u64 `indexOff`, u64 `indexSize`, 24-byte `indexNonce`, 8 reserved zero bytes.
  - Little-endian. AEAD tag 16 bytes, nonce 24 bytes, key 32 bytes.
- **Encryption:**
  - entries use `crypto_aead_lock(..., ad = path bytes, ...)`;
  - the index uses `ad = the 64 header bytes`, with the `indexNonce` field set before sealing.
- **Nonces:** the first 24 bytes of keyed BLAKE2b-256 (key = pack key) over:
  - entries: `"nova-entry" || path || 0x00 || original bytes`;
  - index: `"nova-index" || index plaintext`.
- **Compression:** zstd level 19, kept only if `compressed ≤ 0.95 × original`. Index entries are sorted by path; paths are `/`-separated and relative to the assets root.
- **Key:** 32 bytes as 64 lowercase hex characters. Resolution: env `NOVA_PACK_KEY`, else `<game>/.nova/pack.key`, else generate it (OS CSPRNG) and print a CMake `WARNING` to back it up. `.nova/` is gitignored everywhere.
- **Release-type configurations:** `Release`, `MinSizeRel`, `RelWithDebInfo`. `NOVA_PACK_ASSETS` (default OFF) forces packing in any configuration.
- **Tamper or missing pack in release-type builds:** exit code 1, no output. Dev builds (`NOVA_DEV_TOOLS` defined) log `Asset pack integrity failure: <reason>`, then exit 1.
- **`NOVA_DEV_TOOLS`:** defined for non-release configurations. It gates `NOVA_RENDER_DRIVER`, `NOVA_LOG_INPUT`, `NOVA_DEBUG_OVERLAY`, `NOVA_ALLOC_TRAP` and the F1/three-finger overlay toggle.
- **Hardening (release-type configs only, `nova` and game targets):**
  - compile: `-fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections`;
  - IPO, where `check_ipo_supported`;
  - link: Apple Ninja `-Wl,-dead_strip -Wl,-x`; Android `-Wl,--gc-sections`;
  - Xcode: `DEPLOYMENT_POSTPROCESSING=YES`, `STRIP_INSTALLED_PRODUCT=YES`, `STRIP_STYLE=all`.
- **Devices:** never touch physical devices. Android checks use `ANDROID_SERIAL=emulator-5554` only.

## Review Focus

These five failure modes are each pinned by a named test:

1. **Switching Debug ↔ Release in the same Xcode/Ninja build** must not leave stale loose files next to the pack, or a stale pack next to loose files. Test: `test_stage_switches_modes_cleanly` (Task 3).
2. **Asset names with spaces or non-ASCII characters**, which loose files allow, must round-trip through the pack. Tests: `test_pack_paths_with_spaces_and_unicode` (Task 1) and `Pack reads entries whose paths contain spaces` (Task 2).
3. **A stale `.nova/pack.key` or a different key than the pack was built with** must fail loudly in dev builds (reason logged) and silently in release. Test: `Pack mount fails on wrong key` (Task 2), plus the release run with a wrong-key pack (Task 6).
4. **Android building two ABIs at once**, both writing the same pack, must not corrupt it. Test: `test_concurrent_packs_to_same_output_are_valid` (Task 1).
5. **Large and incompressible assets** (e.g. a 16 MiB random file) must round-trip and be stored uncompressed, without excessive time. Test: `test_large_incompressible_file_round_trips` (Task 1).

---

## File Structure

```
third_party/monocypher/, third_party/zstd/          NEW submodules
cmake/NovaThirdParty.cmake                           MOD  nova_monocypher, nova_zstd_decompress static libs
engine/src/platform/pack_format.hpp/.cpp             NEW  shared format: header/index codec, nonce derivation, seal/open, compress/decompress
tools/nova_pack/CMakeLists.txt, main.cpp             NEW  host tool: keygen, embed-key, pack, verify, list
tools/test_nova_pack.py                              NEW  tool tests (builds the tool into a temp dir)
engine/src/platform/pack_reader.hpp/.cpp             NEW  runtime reader (mount, find, open entry → SDL_IOStream)
engine/src/platform/asset_io.hpp/.cpp                NEW  openAsset / mountPack / integrity failure / packKey decl
engine/src/platform/file.cpp, render/renderer.cpp, audio/audio.cpp, text/text.cpp   MOD  use openAsset
engine/src/app/app.cpp, app/entry.cpp, input/input.cpp   MOD  mount pack, setPackRequired, NOVA_DEV_TOOLS gating
engine/tests/test_pack.cpp, engine/tests/data/test.key, engine/tests/support/test_pack_key.cpp   NEW
cmake/Nova.cmake                                     MOD  build host tool, NOVA_PACK_ASSETS, NOVA_DEV_TOOLS, hardening
cmake/NovaGame.cmake                                 MOD  key resolution + embed, staging post-build, NOVA_REQUIRE_PACK
cmake/NovaStageAssets.cmake                          NEW  -P script: stage loose files or pack, with manifest cleanup
tools/test_stage_assets.py                           NEW  staging script tests
platforms/android/nova-app.gradle                    MOD  release packing, per-variant asset dirs, noCompress, task deps
.gitignore, templates/game/gitignore.template, docs/NEW_GAME.md, README.md, templates/game/README.md   MOD
```

---

### Task 1: Dependencies, shared pack format, and the `nova_pack` host tool

**Files:**
- Create: `engine/src/platform/pack_format.hpp`, `engine/src/platform/pack_format.cpp`, `tools/nova_pack/CMakeLists.txt`, `tools/nova_pack/main.cpp`, `tools/test_nova_pack.py`
- Modify: `cmake/NovaThirdParty.cmake`, `.gitmodules` (via `git submodule add`)

**Interfaces:**
- Produces, in `pack_format.hpp`, namespace `nova::pack`, std types only (`uint8_t` etc.; no SDL, no nova headers), so the host tool can compile it standalone:
  - Constants: `kMagic[8]`, `kVersion = 1`, `kHeaderSize = 64`, `kKeySize = 32`, `kNonceSize = 24`, `kTagSize = 16`, `kZstdLevel = 19`.
  - `using Key = std::array<uint8_t, 32>; using Nonce = std::array<uint8_t, 24>;`
  - `struct Header { uint64_t indexOff, indexSize; Nonce indexNonce; };` with `std::array<uint8_t,64> encodeHeader(const Header&)` and `bool decodeHeader(const uint8_t* bytes64, Header& out)` (false on bad magic, version or reserved bytes).
  - `struct Entry { std::string path; uint64_t dataOff; uint32_t storedSize; uint32_t originalSize; uint8_t compression; Nonce nonce; };`
  - `std::vector<uint8_t> encodeIndex(const std::vector<Entry>&)` and `bool decodeIndex(const uint8_t*, size_t, std::vector<Entry>& out)` (bounds-checked; false on malformed input).
  - `Nonce deriveNonce(const Key&, std::string_view domain, const uint8_t* a, size_t aLen, const uint8_t* b, size_t bLen)`.
  - `void seal(const Key&, const Nonce&, const uint8_t* ad, size_t adLen, const uint8_t* plain, size_t n, uint8_t* cipherOut, uint8_t tagOut[16])` and `bool open(...)` (same arguments, plain out; false if authentication fails).
  - `bool compress(const uint8_t*, size_t, std::vector<uint8_t>& out)` (returns false if not worth it, i.e. > 0.95×) and `bool decompress(const uint8_t*, size_t, size_t originalSize, std::vector<uint8_t>& out)`.
  - `bool parseKeyHex(std::string_view, Key&)` and `std::string keyToHex(const Key&)`.
- Produces, as CMake targets in `NovaThirdParty.cmake`:
  - `nova_monocypher`: static library from `src/monocypher.c`, include dir `src`.
  - `nova_zstd_decompress`: static library from `lib/common/*.c` and `lib/decompress/*.c`, defining `ZSTD_DISABLE_ASM`, include dir `lib`.
  - Both use their own default flags, not `nova_set_warnings`.
- Produces the host tool `nova_pack`, from `tools/nova_pack/CMakeLists.txt`, a standalone project:
  - **Build:** C++17, compiling `${NOVA_ENGINE_DIR}/engine/src/platform/pack_format.cpp`, Monocypher, and zstd `lib/common`, `lib/compress` and `lib/decompress`, with `-DZSTD_DISABLE_ASM`. `NOVA_ENGINE_DIR` is derived from the CMakeLists location, `../..`.
  - **Commands** (exit 0 on success, otherwise 1 with `error: <msg>` on stderr):
    - `nova_pack keygen <file>`: creates the parent directory, writes 64 hex characters plus a newline, and refuses to overwrite.
    - `nova_pack embed-key <keyfile> <out.cpp>`: if `NOVA_PACK_KEY` is set and non-empty it takes precedence; writes the split-key C++ described in spec §4.
    - `nova_pack pack <assets-dir> <out.pak> [--key-file <f>]`: env key first. Writes to `<out>.tmp.<pid>` and then renames it atomically.
    - `nova_pack verify <in.pak> [--key-file <f>]`: decrypts and decompresses every entry.
    - `nova_pack list <in.pak> [--key-file <f>]`: prints `<path>\t<originalSize>\t<storedSize>\t<zstd|raw>` lines.
  - **Randomness:** `arc4random_buf` (Apple) or `getrandom` (Linux) for keygen and the key shares.

- [ ] **Step 1: Add the submodules.**

  ```bash
  git clone -q --depth 1 --branch 4.0.3 https://github.com/LoupVaillant/Monocypher.git third_party/monocypher && git submodule add -q https://github.com/LoupVaillant/Monocypher.git third_party/monocypher
  git clone -q --depth 1 --branch v1.5.7 https://github.com/facebook/zstd.git third_party/zstd && git submodule add -q https://github.com/facebook/zstd.git third_party/zstd
  ```

  Expected: `git submodule status` shows `third_party/monocypher (4.0.3)` and `third_party/zstd (v1.5.7)`.

- [ ] **Step 2: Write the failing tests** in `tools/test_nova_pack.py`.
  - The `setUpClass` builds the tool once: `cmake -S tools/nova_pack -B <tmp>/build -DCMAKE_BUILD_TYPE=Release && cmake --build ...`. It uses `NOVA_PACK_TOOL` instead if that env var points at an existing binary.
  - A helper `run(*args, env=None) -> CompletedProcess` runs the tool.
  - Each test uses a temp assets dir containing `a.png` (random 2 KB), `sfx/b.wav` (20 KB of a sine wave), `fonts/c.ttf` (a copy of `games/brickbreaker/assets/fonts/Fredoka.ttf`) and `README.txt`.

```python
def test_keygen_writes_64_hex_and_refuses_overwrite(self): ...   # re.fullmatch(r"[0-9a-f]{64}\n", text); second keygen → returncode 1
def test_pack_verify_list_round_trip(self):                       # pack → verify rc 0; list has 4 lines sorted by path; wav and ttf "zstd", png "raw"
def test_pack_is_deterministic(self):                             # pack twice → identical bytes
def test_header_and_names_not_visible(self):                      # pak[:8] == b"NOVAPAK\0"; b"sfx/b.wav" not in pak; b"Fredoka" not in pak
def test_tamper_detected_by_verify(self):                         # flip one byte at offsets 70 (data), len-5 (index tag), 20 (header) → verify rc 1 each
def test_wrong_key_fails_and_env_key_takes_precedence(self):      # verify with other key file → rc 1; NOVA_PACK_KEY=<right> with --key-file wrong → rc 0
def test_pack_paths_with_spaces_and_unicode(self):                # files "ui/my button.png", "über/ß.txt" → list shows both paths exactly; verify rc 0
def test_large_incompressible_file_round_trips(self):             # 16 MiB os.urandom file → list shows raw, storedSize == 16 MiB; verify rc 0; pack < 10 s
def test_concurrent_packs_to_same_output_are_valid(self):         # two Popen pack → same out.pak simultaneously; both rc 0; verify rc 0
def test_embed_key_splits_key(self):                              # out.cpp compiles with c++ -std=c++17 + a tiny main printing the key hex == key file; key hex bytes not in out.cpp as one 32-byte run
def test_missing_assets_dir_errors_and_leaves_no_output(self):    # rc 1; out.pak and *.tmp.* absent
```

- [ ] **Step 3: Run, expect a failure.** Run: `python3 -m unittest tools/test_nova_pack.py -v`. Expected: the build of `tools/nova_pack` fails (missing CMakeLists), so every test errors.
- [ ] **Step 4: Implement** `pack_format.{hpp,cpp}`, the `tools/nova_pack` project and `main.cpp`, and the two library targets in `NovaThirdParty.cmake`. Pack algorithm:
  1. Walk the files, skipping dot-files.
  2. Sort the paths.
  3. For each file: read it, compress if worth it, derive the nonce, seal with `ad = path`, and append the ciphertext and tag at the current offset (starting at 64).
  4. Build the index, write a header with `indexOff`/`indexSize` and the derived `indexNonce`, then seal the index with `ad = header bytes`.
  5. Write the header bytes, the data, and the index ciphertext and tag.
- [ ] **Step 5: Run.** `python3 -m unittest tools/test_nova_pack.py -v` → `OK` (11 tests).
- [ ] **Step 6: Commit.** `feat(tools): nova_pack host tool and shared pack format (Monocypher + zstd)`

---

### Task 2: Runtime pack reader and `openAsset`

**Files:**
- Create: `engine/src/platform/pack_reader.hpp/.cpp`, `engine/src/platform/asset_io.hpp/.cpp`, `engine/tests/test_pack.cpp`, `engine/tests/data/test.key`, `engine/tests/support/test_pack_key.cpp`
- Modify: `cmake/Nova.cmake` (build the host tool at configure time), `engine/CMakeLists.txt` (link `nova_monocypher` and `nova_zstd_decompress` PRIVATE), `engine/tests/CMakeLists.txt`, `engine/src/platform/file.cpp`, `engine/src/render/renderer.cpp`, `engine/src/audio/audio.cpp`, `engine/src/text/text.cpp`

**Interfaces:**
- Consumes: the `nova::pack` functions (Task 1).
- Produces:
  - **`Nova.cmake`:** sets `NOVA_PACK_TOOL`, the path of the host-built `nova_pack`. It's built by `execute_process` (configure plus build) into `${CMAKE_BINARY_DIR}/nova-host` from `tools/nova_pack`, passing `-DCMAKE_TOOLCHAIN_FILE=` and not forwarding `CMAKE_OSX_*`, so iOS and Android configures still produce a tool for the build machine. A failure there is a `FATAL_ERROR`.
  - `asset_io.hpp`, namespace `nova`:
    - `namespace detail { void packKey(uint8_t out[32]); }` is declared here and defined by the generated key file (games) or by `test_pack_key.cpp` (tests).
    - `enum class MountResult { Mounted, NotPresent, Corrupt };`
    - `MountResult mountPack(std::string_view rel = "game.pak");`, `bool packMounted();`, `void unmountPack();`
    - `SDL_IOStream* openAsset(std::string_view rel);` reads from the pack when mounted, else `SDL_IOFromFile(assetPath(rel), "rb")`. It returns nullptr for a missing asset; the caller closes the stream.
    - `void reportIntegrityFailure(const char* reason);` logs if `NOVA_DEV_TOOLS` is defined, then calls `std::_Exit(1)`, unless a test handler is installed. This is the spec's `assetIntegrityFailure`, renamed and not `[[noreturn]]` so a test handler can return. Callers then return nullptr or `MountResult::Corrupt`.
    - Under `NOVA_TESTING`: `void setIntegrityFailureHandlerForTests(void (*)(const char*));` and `const pack::Entry* packEntryForTests(std::string_view path);`
  - `PackReader`: `static std::unique_ptr<PackReader> open(SDL_IOStream* io, const pack::Key& key, std::string& error)` (takes ownership of `io`; nullptr on a bad header or index), `const pack::Entry* find(std::string_view path) const` (binary search), `SDL_IOStream* openEntry(const pack::Entry&, std::string& error)` (an `SDL_IOFromDynamicMem` holding the original bytes, seeked to 0; nullptr plus error on auth or decompression failure). Its destructor calls `crypto_wipe` on the key.
  - **Callers changed to use `openAsset`:**
    - `readFile` and `readTextFile`: `SDL_LoadFile_IO(io, &size, true)`;
    - `loadTexture`: `SDL_LoadPNG_IO(io, true)`;
    - `loadSound` and `loadMusic`: `MIX_LoadAudio_IO(mixer, io, predecode, true)`;
    - `loadFont`: `TTF_OpenFontIO(io, true, size)`.
- **Tests:**
  - `engine/tests/data/test.key` holds a fixed 64-character hex key; `test_pack_key.cpp` defines `packKey` from that same literal.
  - A custom command in `engine/tests/CMakeLists.txt` runs `${NOVA_PACK_TOOL} pack ${NOVA_ENGINE_DIR}/games/brickbreaker/assets ${CMAKE_BINARY_DIR}/test-pack/game.pak --key-file data/test.key`.
  - `nova_tests` depends on that command and gets the definition `NOVA_TEST_PACK="<path>"`.

- [ ] **Step 1: Write the failing tests** in `engine/tests/test_pack.cpp`.
  - A fixture installs a recording failure handler.
  - A helper `mountTestPack()` points the asset root at `${CMAKE_BINARY_DIR}/test-pack/`, a folder holding only `game.pak`, so a successful load proves the pack was used rather than loose files. It then calls `mountPack()`.
  - Tests that flip a byte work on a temporary copy of the pack.

```cpp
TEST_CASE("Pack mounts and every asset is byte-identical to the loose file")   // walk brickbreaker assets dir; for each file: openAsset via pack == SDL_LoadFile(loose)
TEST_CASE("Pack is used by readTextFile, textures, sounds and fonts")          // readTextFile("sprites.atlas") ok; RendererBackend(software)->loadTexture("sprites.png") valid & 256x256; AudioBackend::create()->loadSound("sfx/hit.wav") ok; TextBackend loadFont("fonts/Fredoka.ttf@32") ok — asset root contains only game.pak
TEST_CASE("Pack compresses wav and ttf but stores png raw")                    // packEntryForTests("music/loop.wav")->compression == 1; "sprites.png" == 0
TEST_CASE("Pack unknown path is a normal missing asset")                       // openAsset("nope.png") == nullptr; failure handler not called
TEST_CASE("Pack mount fails on wrong key")                                     // copy pack, rebuild with other key via tool → mountPack == Corrupt; handler called with reason containing "index"
TEST_CASE("Pack detects a flipped byte in header, index and entry data")       // header byte 20 → mount Corrupt; last byte → Corrupt; byte 100 → mount ok, openAsset(<entry at 64>) == nullptr and handler called "entry"
TEST_CASE("Pack detects truncation")                                           // drop last 100 bytes → Corrupt
TEST_CASE("Pack reads entries whose paths contain spaces")                     // tool-built pack from a temp dir with "ui/my button.txt" → openAsset reads it
TEST_CASE("No pack present means loose files")                                 // mountPack("absent.pak") == NotPresent; openAsset("sprites.atlas") from loose root works
```

- [ ] **Step 2: Run, expect a failure.** Run: `cmake --build --preset macos-debug`. Expected: compile errors (`platform/asset_io.hpp` not found).
- [ ] **Step 3: Implement** the reader, `asset_io`, and the four caller changes. `mountPack` reads the key with `detail::packKey`, then calls `crypto_wipe` on the local copy after constructing the reader.
- [ ] **Step 4: Run.** `cmake --build --preset macos-debug && ctest --preset macos-debug` → all tests pass (the previous 83 plus 9 new). Also run `build/macos-debug/games/brickbreaker/...brickbreaker --autoplay --frames 1800 --fail-on-alloc` (loose mode) → exit 0.
- [ ] **Step 5: Commit.** `feat(engine): pack reader and openAsset; all asset loads go through it`

---

### Task 3: Build integration — host tool, key, staging, `NOVA_REQUIRE_PACK`, dev switches, hardening (Apple)

**Files:**
- Create: `cmake/NovaStageAssets.cmake`, `tools/test_stage_assets.py`
- Modify: `cmake/Nova.cmake`, `cmake/NovaGame.cmake`, `engine/src/app/entry.cpp`, `engine/src/app/app.cpp`, `engine/src/app/app.hpp`, `engine/src/input/input.cpp`, `engine/src/render/renderer.cpp`, `.gitignore`

**Interfaces:**
- Consumes: the `nova_pack` CLI (Task 1); `NOVA_PACK_TOOL`, `mountPack`, `reportIntegrityFailure` and `MountResult` (Task 2).
- Produces:
  - **`Nova.cmake`:**
    - The option `NOVA_PACK_ASSETS`, and `NOVA_RELEASE_CONFIGS` = `Release;MinSizeRel;RelWithDebInfo`.
    - The `NOVA_DEV_TOOLS` compile definition on `nova` (PUBLIC) for non-release configs.
    - `nova_apply_hardening(<target>)`, applied to `nova` and to each game target.
  - **`nova_add_game` additions:**
    - Resolve the key per spec §4.
    - Run `nova_pack embed-key` into `${CMAKE_CURRENT_BINARY_DIR}/nova_pack_key.cpp` and add it to the target.
    - Remove the per-file `MACOSX_PACKAGE_LOCATION` asset sources on Apple.
    - Add a `POST_BUILD` command: `${CMAKE_COMMAND} -DPACK=$<OR:$<IN_LIST:$<CONFIG>,${NOVA_RELEASE_CONFIGS}>,$<BOOL:${NOVA_PACK_ASSETS}>> -DSRC=<assets> -DDEST=<bundle resources dir> -DTOOL=${NOVA_PACK_TOOL} -DKEYFILE=<keyfile> -P ${NOVA_ENGINE_DIR}/cmake/NovaStageAssets.cmake`.
    - The same expression becomes a compile definition, `NOVA_REQUIRE_PACK=1`, on the game target.
    - The macOS resources dir is `$<TARGET_BUNDLE_CONTENT_DIR:t>/Resources`; the iOS one is `$<TARGET_BUNDLE_CONTENT_DIR:t>`.
  - **`NovaStageAssets.cmake` contract:**
    - It reads the manifest `DEST/.nova-staged`, which lists files staged last time, one relative path per line, and deletes those files.
    - **Pack mode:** runs `TOOL pack SRC DEST/game.pak --key-file KEYFILE` and writes the manifest with `game.pak`.
    - **Loose mode:** copies every file under SRC to DEST (skipping dot-files), and writes the manifest listing them.
    - A tool failure means `FATAL_ERROR`.
  - **`entry.cpp`:** before `appInit`, calls `nova::detail::setPackRequired(NOVA_REQUIRE_PACK_VALUE)`, where `NOVA_REQUIRE_PACK_VALUE` is 1 if `NOVA_REQUIRE_PACK` is defined, else 0. `void setPackRequired(bool)` is declared in `asset_io.hpp`.
  - **`App::init`:** calls `mountPack()` (the default `"game.pak"`) before creating the asset services. `Corrupt`, or `NotPresent` while the pack is required, calls `reportIntegrityFailure("pack missing" / reason)`.
  - **Dev switches:** `SDL_getenv("NOVA_RENDER_DRIVER" | "NOVA_LOG_INPUT" | "NOVA_DEBUG_OVERLAY" | "NOVA_ALLOC_TRAP")` reads and the F1/three-finger toggle are wrapped in `#if defined(NOVA_DEV_TOOLS)`.
  - **`.gitignore`:** adds `**/.nova/`.

- [ ] **Step 1: Write the failing tests** in `tools/test_stage_assets.py`. They run `cmake -P cmake/NovaStageAssets.cmake` against temp dirs, using the tool built by `tools/test_nova_pack.py`'s helper (import and reuse it).

```python
def test_loose_mode_copies_tree_and_writes_manifest(self):   # DEST has a.png, sfx/b.wav; .nova-staged lists both; no game.pak
def test_pack_mode_writes_only_pack(self):                    # DEST has game.pak only (plus manifest); verify rc 0
def test_stage_switches_modes_cleanly(self):                  # loose → pack: no a.png left; pack → loose: no game.pak left; unrelated DEST/Info.plist untouched both ways
def test_tool_failure_is_fatal(self):                         # KEYFILE missing → cmake -P rc != 0
```

  Then add the engine-level failing check: `cmake --preset macos-release && cmake --build --preset macos-release && ls build/macos-release/games/brickbreaker/brickbreaker.app/Contents/Resources`. Expected now: loose `sprites.png` etc., and no `game.pak`.

- [ ] **Step 2: Run, expect a failure.** Run: `python3 -m unittest tools/test_stage_assets.py -v`. Expected: an error (the script doesn't exist).
- [ ] **Step 3: Implement** everything in the Interfaces block.
- [ ] **Step 4: Verify.**
  - Run: `python3 -m unittest tools/test_stage_assets.py tools/test_nova_pack.py -v`. Expected: `OK`.
  - Debug: run `cmake --preset macos-debug && cmake --build --preset macos-debug && ctest --preset macos-debug`. Expected: all pass, and the debug `.app` `Resources` holds loose assets with no `game.pak`. Then run `brickbreaker --autoplay --frames 1800 --fail-on-alloc`. Expected: exit 0.
  - Release: run `cmake --preset macos-release && cmake --build --preset macos-release`. Expected: `Resources` contains only `game.pak` (plus `.nova-staged` and Apple's files). `grep -rlE "sprites|\.wav|Fredoka" build/macos-release/games/brickbreaker/brickbreaker.app` finds nothing, and `nm -gU <exe> | grep -c nova` prints `0`.
  - Release smoke: `<release exe> --autoplay --frames 1800; echo $?` → `0`.
  - Tampering: in a copy of the `.app` with one byte flipped in `game.pak` at offset 200, run `--frames 60 > out 2>&1; echo $?`. Expected: `1`, and `out` is empty. A copy with `game.pak` deleted gives the same result.
  - Key file: `games/brickbreaker/.nova/pack.key` exists, `git check-ignore` matches it, and the CMake configure output contained the back-up `WARNING` the first time.
- [ ] **Step 5: Commit.** `build: release builds stage an encrypted game.pak; hardening and dev-switch gating`

---

### Task 4: iOS release verification

**Files:** none expected. Fixes to Task 3 files go here if needed.

- [ ] **Step 1: Release Simulator build.** Run `cmake --preset ios && xcodebuild -project build/ios/Nova.xcodeproj -scheme brickbreaker -configuration Release -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' -derivedDataPath build/ios/DerivedData CODE_SIGNING_ALLOWED=NO build | tail -1`. Expected: `** BUILD SUCCEEDED **`.
- [ ] **Step 2: Inspect the bundle.** Run `ls build/ios/games/brickbreaker/Release-iphonesimulator/brickbreaker.app`. Expected: `game.pak` present; no `sprites.png`, `sfx`, `fonts` or `music`; `Assets.car` present (the icon). Also run `grep -rlE "sprites|Fredoka" <app>` → nothing.
- [ ] **Step 3: Launch.** Launch the app on the booted iPhone 15 Pro Simulator (simulator tool `launch`), wait 3 s, and take a screenshot. Expected: the menu renders, and `log show` contains `Brick Breaker started`.
- [ ] **Step 4: Debug still loose.** Build the Debug configuration → its bundle has `sprites.png` and no `game.pak`.
- [ ] **Step 5: Commit fixes, if any.** `fix(ios): ...`

---

### Task 5: Android release packing

**Files:**
- Modify: `platforms/android/nova-app.gradle`, `cmake/NovaGame.cmake` (Android staging destination)

**Interfaces:**
- Consumes: the staging script and pack mode (Task 3).
- Produces:
  - **`nova-app.gradle`:**
    - `buildTypes.release.externalNativeBuild.cmake.arguments += ['-DNOVA_PACK_ASSETS=ON', "-DNOVA_ANDROID_PACK_DIR=${gameRoot}/build/android-pack/release"]`.
    - `sourceSets.main.assets.srcDirs = []`, `sourceSets.debug.assets.srcDirs = ["${gameRoot}/assets"]`, `sourceSets.release.assets.srcDirs = ["${gameRoot}/build/android-pack/release"]`.
    - `androidResources { noCompress 'pak' }`.
    - `applicationVariants.all { v -> tasks["merge${v.name.capitalize()}Assets"].dependsOn("externalNativeBuild${v.name.capitalize()}") }`.
  - **`nova_add_game` (Android):** when `NOVA_ANDROID_PACK_DIR` is set, adds the `POST_BUILD` staging command on `main` with `DEST=${NOVA_ANDROID_PACK_DIR}` (pack mode is forced by `NOVA_PACK_ASSETS=ON`).

- [ ] **Step 1: Failing check.** Run `cd games/brickbreaker/platforms/android && ./gradlew assembleRelease && unzip -l app/build/outputs/apk/release/app-release-unsigned.apk | grep -c "assets/"`. Expected now: more than 1 (loose assets), and no `game.pak`.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Verify.**
  - **Release APK contents:** `unzip -lv .../app-release-unsigned.apk | grep assets/` → only `assets/game.pak`, with method `Stored`.
  - **Debug APK unchanged:** `assembleDebug` → loose assets as before, and no `game.pak`.
  - **On the emulator:** sign the release APK for the emulator only, with `$ANDROID_HOME/build-tools/36.0.0/apksigner sign --ks ~/.android/debug.keystore --ks-pass pass:android --out <scratch>/bb-release.apk app-release-unsigned.apk` (zipalign first if needed). Then `ANDROID_SERIAL=emulator-5554 adb install -r <scratch>/bb-release.apk`, launch, and check logcat (`SDL/APP`) → `Renderer backend: vulkan` and `Brick Breaker started`. Uninstall it afterwards and reinstall the debug build.
- [ ] **Step 4: Commit.** `build(android): release variant ships only game.pak (stored)`

---

### Task 6: Generated games, docs, end-to-end

**Files:**
- Modify: `templates/game/gitignore.template` (`.nova/`), `templates/game/README.md`, `docs/NEW_GAME.md` (new "Protecting assets" section per spec §9), `README.md`, `tools/test_new_game.py` (assert `.nova/` in the generated `.gitignore`)

- [ ] **Step 1: Failing test.** Add `test_gitignore_protects_pack_key` to `tools/test_new_game.py`: `".nova/"` must be in the generated `.gitignore`. Run → FAIL.
- [ ] **Step 2: Implement** the template change and the docs.
- [ ] **Step 3: Generated game end to end.**
  1. Generate `<scratch>/Pack Studio/Pack Test`.
  2. In it, run `cmake --preset macos-release && cmake --build --preset macos-release`. Expected: `.nova/pack.key` is created and `git check-ignore .nova/pack.key` matches; the `.app` has only `game.pak`; `--autoplay --frames 300` exits 0.
  3. Wrong key: replace `.nova/pack.key` with a new key, *without* rebuilding the pack, by copying the old pack into a freshly built app. The release exe exits 1 silently. Separately, the debug build of the same mismatch logs `Asset pack integrity failure`.
  4. Delete the scratch game.
- [ ] **Step 4: Run all test suites.** `python3 -m unittest tools/test_new_game.py tools/test_pack_atlas.py tools/test_nova_pack.py tools/test_stage_assets.py` → OK, and `ctest --preset macos-debug` → all pass.
- [ ] **Step 5: Commit.** `docs: protecting assets; template ignores .nova/`

---

### Task 7: Final review, merge, push

- [ ] **Step 1:** Run the whole-branch review (executing-plans final review), then do one fix pass, test-first.
- [ ] **Step 2:** Fast-forward `main`, re-run all suites, push `main`, and delete `feat/asset-pack`.
