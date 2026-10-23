# Engine repo: builds the engine, its tests and the Brick Breaker sample. Games outside this
# repo include cmake/Nova.cmake from their own top-level CMakeLists.txt in exactly the same way.
cmake_minimum_required(VERSION 3.24)
project(Nova C CXX)

include(cmake/Nova.cmake)

add_subdirectory(games/brickbreaker)
