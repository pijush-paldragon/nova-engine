# Configures and adds every third-party submodule. Each library is pinned to a
# release tag (see .gitmodules); options here strip tests, samples and unused codecs.

set(NOVA_THIRD_PARTY_DIR "${NOVA_ENGINE_DIR}/third_party")
set(NOVA_THIRD_PARTY_BINARY_DIR "${CMAKE_BINARY_DIR}/nova-third-party")

macro(nova_force_option name value)
    set(${name} ${value} CACHE BOOL "" FORCE)
endmacro()

# --- SDL3 -------------------------------------------------------------------
# Android's SDLActivity loads libSDL3.so at runtime, so SDL must be shared there.
if(ANDROID)
    nova_force_option(SDL_SHARED ON)
    nova_force_option(SDL_STATIC OFF)
else()
    nova_force_option(SDL_SHARED OFF)
    nova_force_option(SDL_STATIC ON)
endif()
nova_force_option(SDL_TEST_LIBRARY OFF)
nova_force_option(SDL_TESTS OFF)
nova_force_option(SDL_EXAMPLES OFF)
add_subdirectory("${NOVA_THIRD_PARTY_DIR}/SDL" "${NOVA_THIRD_PARTY_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)

# Satellite libraries are always static and link against the SDL3 target above.
set(NOVA_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)

# --- SDL3_ttf ---------------------------------------------------------------
nova_force_option(SDLTTF_VENDORED ON)
nova_force_option(SDLTTF_HARFBUZZ OFF)
nova_force_option(SDLTTF_PLUTOSVG OFF)
nova_force_option(SDLTTF_SAMPLES OFF)
nova_force_option(SDLTTF_INSTALL OFF)
add_subdirectory("${NOVA_THIRD_PARTY_DIR}/SDL_ttf" "${NOVA_THIRD_PARTY_BINARY_DIR}/SDL_ttf" EXCLUDE_FROM_ALL)

# --- SDL3_mixer: WAV + Ogg Vorbis (stb_vorbis, header-only) only ---------------
nova_force_option(SDLMIXER_VENDORED OFF)
nova_force_option(SDLMIXER_INSTALL OFF)
nova_force_option(SDLMIXER_TESTS OFF)
nova_force_option(SDLMIXER_EXAMPLES OFF)
nova_force_option(SDLMIXER_WAVE ON)
nova_force_option(SDLMIXER_VORBIS_STB ON)
foreach(opt AIFF VOC AU FLAC FLAC_LIBFLAC FLAC_DRFLAC GME MOD MOD_XMP MOD_XMP_LITE MP3 MP3_DRMP3
            MP3_MPG123 MIDI MIDI_FLUIDSYNTH MIDI_TIMIDITY OPUS VORBIS_VORBISFILE VORBIS_TREMOR
            WAVPACK WAVPACK_DSD)
    nova_force_option(SDLMIXER_${opt} OFF)
endforeach()
add_subdirectory("${NOVA_THIRD_PARTY_DIR}/SDL_mixer" "${NOVA_THIRD_PARTY_BINARY_DIR}/SDL_mixer" EXCLUDE_FROM_ALL)

# --- Box2D ------------------------------------------------------------------
nova_force_option(BOX2D_SAMPLES OFF)
nova_force_option(BOX2D_BENCHMARKS OFF)
nova_force_option(BOX2D_DOCS OFF)
nova_force_option(BOX2D_UNIT_TESTS OFF)
add_subdirectory("${NOVA_THIRD_PARTY_DIR}/box2d" "${NOVA_THIRD_PARTY_BINARY_DIR}/box2d" EXCLUDE_FROM_ALL)

# --- EnTT (header-only) -----------------------------------------------------
add_subdirectory("${NOVA_THIRD_PARTY_DIR}/entt" "${NOVA_THIRD_PARTY_BINARY_DIR}/entt" EXCLUDE_FROM_ALL)

# --- Monocypher (asset pack encryption) ---------------------------------------
add_library(nova_monocypher STATIC "${NOVA_THIRD_PARTY_DIR}/monocypher/src/monocypher.c")
target_include_directories(nova_monocypher PUBLIC "${NOVA_THIRD_PARTY_DIR}/monocypher/src")

# --- zstd decompressor (asset pack); the nova_pack host tool builds the compressor itself ---
file(GLOB NOVA_ZSTD_SOURCES
    "${NOVA_THIRD_PARTY_DIR}/zstd/lib/common/*.c" "${NOVA_THIRD_PARTY_DIR}/zstd/lib/decompress/*.c")
add_library(nova_zstd_decompress STATIC ${NOVA_ZSTD_SOURCES})
target_include_directories(nova_zstd_decompress PUBLIC "${NOVA_THIRD_PARTY_DIR}/zstd/lib")
target_compile_definitions(nova_zstd_decompress PRIVATE ZSTD_DISABLE_ASM ZSTD_MULTITHREAD=0)

# --- doctest (desktop tests only) -------------------------------------------
if(NOVA_BUILD_ENGINE_TESTS OR NOVA_BUILD_GAME_TESTS)
    nova_force_option(DOCTEST_WITH_TESTS OFF)
    add_subdirectory("${NOVA_THIRD_PARTY_DIR}/doctest" "${NOVA_THIRD_PARTY_BINARY_DIR}/doctest" EXCLUDE_FROM_ALL)
endif()

set(BUILD_SHARED_LIBS ${NOVA_SAVED_BUILD_SHARED_LIBS})
