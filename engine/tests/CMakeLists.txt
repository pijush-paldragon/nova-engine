file(GLOB NOVA_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# The test pack: Brick Breaker's assets packed with data/test.key (support/test_pack_key.cpp holds the same key).
set(NOVA_TEST_PACK "${CMAKE_BINARY_DIR}/test-pack/game.pak")
file(GLOB_RECURSE _nova_test_pack_inputs CONFIGURE_DEPENDS "${NOVA_ENGINE_DIR}/games/brickbreaker/assets/*")
add_custom_command(OUTPUT "${NOVA_TEST_PACK}"
    COMMAND ${NOVA_PACK_TOOL_UPDATE}
    COMMAND "${NOVA_PACK_TOOL}" pack "${NOVA_ENGINE_DIR}/games/brickbreaker/assets" "${NOVA_TEST_PACK}"
            --key-file "${CMAKE_CURRENT_SOURCE_DIR}/data/test.key"
    DEPENDS ${_nova_test_pack_inputs} ${NOVA_PACK_TOOL_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/data/test.key"
    COMMENT "Packing test assets"
    VERBATIM)
add_custom_target(nova_test_pack DEPENDS "${NOVA_TEST_PACK}")

add_executable(nova_tests ${NOVA_TEST_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/support/counting_allocator.cpp"
                          "${CMAKE_CURRENT_SOURCE_DIR}/support/test_pack_key.cpp")
add_dependencies(nova_tests nova_test_pack)
target_link_libraries(nova_tests PRIVATE nova doctest::doctest SDL3::SDL3 SDL3_ttf::SDL3_ttf
                                         SDL3_mixer::SDL3_mixer box2d::box2d)
# Same RTTI setting as nova so shared templates (EnTT) compile identically; exceptions stay on for doctest.
target_compile_options(nova_tests PRIVATE -fno-rtti)
target_include_directories(nova_tests PRIVATE "${NOVA_ENGINE_DIR}/engine/src" "${CMAKE_CURRENT_SOURCE_DIR}")
# NOVA_TESTING enables test-only hooks in engine headers, for this target only (never games).
target_compile_definitions(nova_tests PRIVATE NOVA_TESTING
    NOVA_TEST_ASSET_ROOT="${NOVA_ENGINE_DIR}/games/brickbreaker/assets/"
    NOVA_TEST_PACK="${NOVA_TEST_PACK}"
    NOVA_TEST_KEY_FILE="${CMAKE_CURRENT_SOURCE_DIR}/data/test.key"
    NOVA_PACK_TOOL="${NOVA_PACK_TOOL}")

include("${NOVA_ENGINE_DIR}/third_party/doctest/scripts/cmake/doctest.cmake")
doctest_discover_tests(nova_tests)
