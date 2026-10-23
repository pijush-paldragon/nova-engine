# nova_pack: builds encrypted asset packs (game.pak). A standalone project so it can be built
# for the build machine even while a game is configured for iOS or Android (see cmake/Nova.cmake).
cmake_minimum_required(VERSION 3.24)
project(nova_pack C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(NOVA_ENGINE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
set(ZSTD_LIB "${NOVA_ENGINE_DIR}/third_party/zstd/lib")

file(GLOB ZSTD_SOURCES
    "${ZSTD_LIB}/common/*.c" "${ZSTD_LIB}/compress/*.c" "${ZSTD_LIB}/decompress/*.c")

add_executable(nova_pack
    main.cpp
    "${NOVA_ENGINE_DIR}/engine/src/platform/pack_format.cpp"
    "${NOVA_ENGINE_DIR}/third_party/monocypher/src/monocypher.c"
    ${ZSTD_SOURCES})
target_include_directories(nova_pack PRIVATE
    "${NOVA_ENGINE_DIR}/engine/src/platform"
    "${NOVA_ENGINE_DIR}/third_party/monocypher/src"
    "${ZSTD_LIB}")
target_compile_definitions(nova_pack PRIVATE NOVA_PACK_HOST_TOOL ZSTD_DISABLE_ASM ZSTD_MULTITHREAD=0)
