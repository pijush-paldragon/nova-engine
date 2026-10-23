# nova_add_game(<name>
#     SOURCES <files...>
#     ASSETS <dir>                    # copied to the bundle Resources / APK assets root
#     [BUNDLE_ID <id>]                # default com.gamestudio.<name>
#     [DISPLAY_NAME <name>]           # default <name>
#     [ORIENTATION Portrait|Landscape])
#
# Creates the game target: an app bundle on Apple platforms, libmain.so on Android
# (the library SDLActivity loads). Game code links the engine but cannot include SDL,
# EnTT internals aside, or any other third-party header.
function(nova_add_game name)
    cmake_parse_arguments(GAME "" "ASSETS;BUNDLE_ID;DISPLAY_NAME;ORIENTATION" "SOURCES" ${ARGN})
    if(NOT GAME_BUNDLE_ID)
        set(GAME_BUNDLE_ID "com.gamestudio.${name}")
    endif()
    if(NOT GAME_DISPLAY_NAME)
        set(GAME_DISPLAY_NAME "${name}")
    endif()
    if(NOT GAME_ORIENTATION)
        set(GAME_ORIENTATION "Portrait")
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
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${GAME_BUNDLE_ID}")

        if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
            set(NOVA_DISPLAY_NAME "${GAME_DISPLAY_NAME}")
            set(NOVA_BUNDLE_ID "${GAME_BUNDLE_ID}")
            if(GAME_ORIENTATION STREQUAL "Landscape")
                set(NOVA_IOS_ORIENTATIONS "<string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string>")
            else()
                set(NOVA_IOS_ORIENTATIONS "<string>UIInterfaceOrientationPortrait</string>")
            endif()
            set(plist "${CMAKE_CURRENT_BINARY_DIR}/${name}-Info.plist")
            configure_file("${NOVA_ENGINE_DIR}/platforms/ios/Info.plist.in" "${plist}" @ONLY)
            set(storyboard "${NOVA_ENGINE_DIR}/platforms/ios/LaunchScreen.storyboard")
            target_sources(${target} PRIVATE "${storyboard}")
            set_source_files_properties("${storyboard}" TARGET_DIRECTORY ${target} PROPERTIES
                MACOSX_PACKAGE_LOCATION Resources)
            set_target_properties(${target} PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST "${plist}"
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
                XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
                XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
                XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")
        endif()
    endif()
endfunction()
