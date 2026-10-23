file(GLOB NOVA_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(nova_tests ${NOVA_TEST_SOURCES})
target_link_libraries(nova_tests PRIVATE nova doctest::doctest SDL3::SDL3 SDL3_ttf::SDL3_ttf
                                         SDL3_mixer::SDL3_mixer box2d::box2d)
# Tests may use exceptions/RTTI (doctest prefers them); the engine library itself does not.
target_compile_options(nova_tests PRIVATE -fexceptions -frtti)
target_include_directories(nova_tests PRIVATE "${CMAKE_SOURCE_DIR}/engine/src")
target_compile_definitions(nova_tests PRIVATE
    NOVA_TEST_ASSET_ROOT="${CMAKE_SOURCE_DIR}/games/brickbreaker/assets/")

include("${CMAKE_SOURCE_DIR}/third_party/doctest/scripts/cmake/doctest.cmake")
doctest_discover_tests(nova_tests)
