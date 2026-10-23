# nova_add_game(<name>
#     SOURCES <files...>
#     ASSETS <dir>                    # copied to the bundle Resources / APK assets root
#     [BUNDLE_ID <id>]                # default com.gamestudio.<name>
#     [DISPLAY_NAME <name>]           # default <name>
#     [ORIENTATION Portrait|Landscape]
#     [VERSION <x.y.z>]               # default 1.0.0 (CFBundleShortVersionString)
#     [BUILD_NUMBER <n>]              # default 1 (CFBundleVersion); bump for every store upload
#     [IOS_PLIST <file>]              # default: <game>/platforms/ios/Info.plist.in, else the engine's
#     [IOS_LAUNCH_SCREEN <file>]      # default: <game>/platforms/ios/LaunchScreen.storyboard, else the engine's
#     [IOS_ASSET_CATALOG <dir>])      # default: <game>/platforms/ios/Assets.xcassets if present (AppIcon)
#
# Creates the game target: an app bundle on Apple platforms, libmain.so on Android
# (the library SDLActivity loads). Game code links the engine but cannot include SDL,
# EnTT internals aside, or any other third-party header.
function(nova_add_game name)
    cmake_parse_arguments(GAME "" "ASSETS;BUNDLE_ID;DISPLAY_NAME;ORIENTATION;VERSION;BUILD_NUMBER;IOS_PLIST;IOS_LAUNCH_SCREEN;IOS_ASSET_CATALOG" "SOURCES" ${ARGN})
    if(GAME_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "nova_add_game(${name}): unknown arguments: ${GAME_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT GAME_BUNDLE_ID)
        set(GAME_BUNDLE_ID "com.gamestudio.${name}")
    endif()
    if(NOT GAME_DISPLAY_NAME)
        set(GAME_DISPLAY_NAME "${name}")
    endif()
    if(NOT GAME_ORIENTATION)
        set(GAME_ORIENTATION "Portrait")
    endif()
    if(NOT GAME_VERSION)
        set(GAME_VERSION "1.0.0")
    endif()
    if(NOT GAME_BUILD_NUMBER)
        set(GAME_BUILD_NUMBER "1")
    endif()
    # Per-game iOS files win over the engine defaults.
    foreach(kind PLIST LAUNCH_SCREEN)
        if(kind STREQUAL "PLIST")
            set(file_name "Info.plist.in")
        else()
            set(file_name "LaunchScreen.storyboard")
        endif()
        if(NOT GAME_IOS_${kind})
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios/${file_name}")
                set(GAME_IOS_${kind} "${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios/${file_name}")
            else()
                set(GAME_IOS_${kind} "${NOVA_ENGINE_DIR}/platforms/ios/${file_name}")
            endif()
        endif()
    endforeach()
    if(NOT GAME_IOS_ASSET_CATALOG AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios/Assets.xcassets")
        set(GAME_IOS_ASSET_CATALOG "${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios/Assets.xcassets")
    endif()
    get_filename_component(assets_dir "${GAME_ASSETS}" ABSOLUTE)

    if(ANDROID)
        set(target main)
        add_library(${target} SHARED ${GAME_SOURCES} ${NOVA_ENTRY_SOURCE})
    else()
        set(target ${name})
        add_executable(${target} MACOSX_BUNDLE ${GAME_SOURCES} ${NOVA_ENTRY_SOURCE})
    endif()

    target_link_libraries(${target} PRIVATE nova)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_options(${target} PRIVATE ${NOVA_NO_EH_RTTI_FLAGS})
    set_target_properties(${target} PROPERTIES NOVA_GAME_NAME "${name}")

    # Only the engine entry point sees SDL headers; game sources cannot include them.
    get_target_property(sdl_target SDL3::SDL3 ALIASED_TARGET)
    if(NOT sdl_target)
        set(sdl_target SDL3::SDL3)
    endif()
    target_link_libraries(${target} PRIVATE $<LINK_ONLY:${sdl_target}>)
    set_property(SOURCE "${NOVA_ENTRY_SOURCE}" TARGET_DIRECTORY ${target} APPEND PROPERTY
        INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:${sdl_target},INTERFACE_INCLUDE_DIRECTORIES>")

    if(APPLE)
        file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS "${assets_dir}/*")
        foreach(file IN LISTS asset_files)
            file(RELATIVE_PATH rel "${assets_dir}" "${file}")
            get_filename_component(rel_dir "${rel}" DIRECTORY)
            set_source_files_properties("${file}" TARGET_DIRECTORY ${target} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources/${rel_dir}")
            target_sources(${target} PRIVATE "${file}")
        endforeach()

        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME "${name}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${GAME_BUNDLE_ID}"
            MACOSX_BUNDLE_BUNDLE_NAME "${GAME_DISPLAY_NAME}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${GAME_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${GAME_BUILD_NUMBER}"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${GAME_BUNDLE_ID}")

        if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
            set(NOVA_DISPLAY_NAME "${GAME_DISPLAY_NAME}")
            set(NOVA_BUNDLE_ID "${GAME_BUNDLE_ID}")
            set(NOVA_VERSION "${GAME_VERSION}")
            set(NOVA_BUILD_NUMBER "${GAME_BUILD_NUMBER}")
            if(GAME_ORIENTATION STREQUAL "Landscape")
                set(NOVA_IOS_ORIENTATIONS "<string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string>")
            else()
                set(NOVA_IOS_ORIENTATIONS "<string>UIInterfaceOrientationPortrait</string>")
            endif()
            set(plist "${CMAKE_CURRENT_BINARY_DIR}/${name}-Info.plist")
            configure_file("${GAME_IOS_PLIST}" "${plist}" @ONLY)
            set(storyboard "${GAME_IOS_LAUNCH_SCREEN}")
            target_sources(${target} PRIVATE "${storyboard}")
            set_source_files_properties("${storyboard}" TARGET_DIRECTORY ${target} PROPERTIES
                MACOSX_PACKAGE_LOCATION Resources)
            if(GAME_IOS_ASSET_CATALOG)
                # Compiled by Xcode's actool; the App Store requires the AppIcon set.
                target_sources(${target} PRIVATE "${GAME_IOS_ASSET_CATALOG}")
                set_source_files_properties("${GAME_IOS_ASSET_CATALOG}" TARGET_DIRECTORY ${target} PROPERTIES
                    MACOSX_PACKAGE_LOCATION Resources)
                set_target_properties(${target} PROPERTIES XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon)
            endif()
            set_target_properties(${target} PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST "${plist}"
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
                XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
                XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
                XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")
        endif()
    endif()
endfunction()

# nova_add_game_tests(<name> SOURCES <files...>)
#
# Desktop-only doctest runner for a game's logic: <name>_tests links the engine, sees the
# game's src/ and reads assets from the game's assets/ folder. No-op when NOVA_BUILD_GAME_TESTS
# is OFF (always on iOS/Android).
function(nova_add_game_tests name)
    cmake_parse_arguments(TESTS "" "" "SOURCES" ${ARGN})
    if(TESTS_UNPARSED_ARGUMENTS OR NOT TESTS_SOURCES)
        message(FATAL_ERROR "nova_add_game_tests(${name}): unknown arguments '${TESTS_UNPARSED_ARGUMENTS}' "
                            "(expected: SOURCES <files...>)")
    endif()
    if(NOT NOVA_BUILD_GAME_TESTS)
        return()
    endif()
    set(target ${name}_tests)
    set(support "${NOVA_ENGINE_DIR}/engine/tests/support")
    add_executable(${target} ${TESTS_SOURCES} "${support}/game_test_main.cpp" "${support}/counting_allocator.cpp")
    target_link_libraries(${target} PRIVATE nova doctest::doctest SDL3::SDL3)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_options(${target} PRIVATE -fno-rtti)
    target_compile_definitions(${target} PRIVATE NOVA_GAME_TEST_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets/")
    include("${NOVA_ENGINE_DIR}/third_party/doctest/scripts/cmake/doctest.cmake")
    doctest_discover_tests(${target})
endfunction()
