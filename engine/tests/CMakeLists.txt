file(GLOB NOVA_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(nova_tests ${NOVA_TEST_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/support/counting_allocator.cpp")
target_link_libraries(nova_tests PRIVATE nova doctest::doctest SDL3::SDL3 SDL3_ttf::SDL3_ttf
                                         SDL3_mixer::SDL3_mixer box2d::box2d)
# Same RTTI setting as nova so shared templates (EnTT) compile identically; exceptions stay on for doctest.
target_compile_options(nova_tests PRIVATE -fno-rtti)
target_include_directories(nova_tests PRIVATE "${NOVA_ENGINE_DIR}/engine/src" "${CMAKE_CURRENT_SOURCE_DIR}")
# NOVA_TESTING enables test-only hooks in engine headers, for this target only (never games).
target_compile_definitions(nova_tests PRIVATE NOVA_TESTING
    NOVA_TEST_ASSET_ROOT="${NOVA_ENGINE_DIR}/games/brickbreaker/assets/")

include("${NOVA_ENGINE_DIR}/third_party/doctest/scripts/cmake/doctest.cmake")
doctest_discover_tests(nova_tests)
