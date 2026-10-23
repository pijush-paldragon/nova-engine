# Brick Breaker works both as part of the engine repo (add_subdirectory from the root) and as
# a top-level project (its Android build, or cmake -S games/brickbreaker), exactly like a game
# living outside the engine.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.24)
    project(BrickBreaker C CXX)
    set(NOVA_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to the nova-engine checkout")
    include("${NOVA_ENGINE_DIR}/cmake/Nova.cmake")
endif()

nova_add_game(brickbreaker
    SOURCES
        src/main.cpp
        src/level.cpp
        src/systems.cpp
        src/scenes/menu_scene.cpp
        src/scenes/play_scene.cpp
        src/scenes/pause_scene.cpp
        src/scenes/game_over_scene.cpp
    ASSETS assets
    BUNDLE_ID com.gamestudio.brickbreaker
    DISPLAY_NAME "Brick Breaker"
    ORIENTATION Portrait
    VERSION 1.0.0
    BUILD_NUMBER 1)

nova_add_game_tests(brickbreaker SOURCES tests/test_level.cpp src/level.cpp)
