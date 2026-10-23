cmake_minimum_required(VERSION 3.24)
project(Nova C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(ANDROID OR IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(NOVA_MOBILE ON)
else()
    set(NOVA_MOBILE OFF)
endif()

set(NOVA_GAME "brickbreaker" CACHE STRING "Game folder under games/ to build")
if(NOVA_MOBILE)
    option(NOVA_BUILD_TESTS "Build engine unit tests" OFF)
else()
    option(NOVA_BUILD_TESTS "Build engine unit tests" ON)
endif()
option(NOVA_SANITIZE "Enable ASan + UBSan" OFF)

# Sanitizers must instrument everything that shares the process, third party included.
if(NOVA_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

include(NovaWarnings)
include(NovaThirdParty)
include(NovaGame)

if(NOVA_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(engine)

if(EXISTS "${CMAKE_SOURCE_DIR}/games/${NOVA_GAME}/CMakeLists.txt")
    add_subdirectory("games/${NOVA_GAME}")
endif()
