# Nova engine entry point. A game's top-level CMakeLists.txt includes this after project():
#
#   include("${NOVA_ENGINE_DIR}/cmake/Nova.cmake")
#
# It adds the third-party libraries and the engine into the game's build tree (from source,
# so every game always builds the current engine) and defines nova_add_game().
include_guard(GLOBAL)

get_filename_component(NOVA_ENGINE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
set(NOVA_ENGINE_DIR "${NOVA_ENGINE_DIR}" CACHE PATH "Path to the nova-engine checkout" FORCE)
list(APPEND CMAKE_MODULE_PATH "${NOVA_ENGINE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(ANDROID OR IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(NOVA_MOBILE ON)
else()
    set(NOVA_MOBILE OFF)
endif()

# Engine tests only when building the engine repo itself; games never build them.
get_filename_component(_nova_top_source "${CMAKE_SOURCE_DIR}" REALPATH)
if(NOT NOVA_MOBILE AND _nova_top_source STREQUAL NOVA_ENGINE_DIR)
    option(NOVA_BUILD_ENGINE_TESTS "Build the engine's unit tests" ON)
else()
    option(NOVA_BUILD_ENGINE_TESTS "Build the engine's unit tests" OFF)
endif()
if(NOVA_MOBILE)
    option(NOVA_BUILD_GAME_TESTS "Build game test targets (nova_add_game_tests)" OFF)
else()
    option(NOVA_BUILD_GAME_TESTS "Build game test targets (nova_add_game_tests)" ON)
endif()
option(NOVA_SANITIZE "Enable ASan + UBSan" OFF)

# Sanitizers must instrument everything that shares the process, third party included.
if(NOVA_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(NOVA_BUILD_ENGINE_TESTS OR NOVA_BUILD_GAME_TESTS)
    enable_testing()
endif()

include(NovaWarnings)
include(NovaThirdParty)
include(NovaGame)

add_subdirectory("${NOVA_ENGINE_DIR}/engine" "${CMAKE_BINARY_DIR}/nova")
