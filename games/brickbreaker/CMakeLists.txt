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
    ORIENTATION Portrait)

# Game logic tests run inside the engine's test runner on desktop.
if(TARGET nova_tests)
    target_sources(nova_tests PRIVATE tests/test_level.cpp src/level.cpp)
    target_include_directories(nova_tests PRIVATE src)
endif()
