file(GLOB_RECURSE NOVA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
# entry.cpp holds SDL_main and the allocator hook; nova_add_game compiles it into each game.
list(FILTER NOVA_SOURCES EXCLUDE REGEX ".*/src/app/entry\\.cpp$")
set(NOVA_ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/app/entry.cpp" CACHE INTERNAL "")

add_library(nova STATIC ${NOVA_SOURCES})
add_library(nova::nova ALIAS nova)

target_include_directories(nova
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(nova PUBLIC cxx_std_20)
# Engine and games build without exceptions/RTTI (games get the flags from nova_add_game);
# kept PRIVATE so the doctest runner can use exceptions.
set(NOVA_NO_EH_RTTI_FLAGS -fno-exceptions -fno-rtti CACHE INTERNAL "")
target_compile_options(nova PRIVATE ${NOVA_NO_EH_RTTI_FLAGS})

# EnTT is public because World's templated API needs it; everything else stays private.
target_link_libraries(nova
    PUBLIC  EnTT::EnTT
    PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_mixer::SDL3_mixer box2d::box2d
            nova_monocypher nova_zstd_decompress)

target_compile_definitions(nova PUBLIC $<$<CONFIG:Debug>:NOVA_DEBUG NOVA_TRACK_ALLOCS>)
nova_set_warnings(nova)

if(NOVA_BUILD_ENGINE_TESTS)
    add_subdirectory(tests)
endif()
