# Release-type configurations only: hidden symbols, dead-code stripping, LTO and stripped
# binaries, so a shipped game exposes as little of its code as possible.
include_guard(GLOBAL)
include(CheckIPOSupported)

check_ipo_supported(RESULT NOVA_IPO_SUPPORTED OUTPUT _nova_ipo_out LANGUAGES C CXX)

function(nova_apply_hardening target)
    target_compile_options(${target} PRIVATE
        "$<${NOVA_IS_RELEASE}:-fvisibility=hidden;-fvisibility-inlines-hidden;-ffunction-sections;-fdata-sections>")
    if(NOVA_IPO_SUPPORTED)
        foreach(config IN LISTS NOVA_RELEASE_CONFIGS)
            string(TOUPPER "${config}" upper)
            set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_${upper} ON)
        endforeach()
    endif()

    get_target_property(type ${target} TYPE)
    if(type STREQUAL "STATIC_LIBRARY")
        return()
    endif()
    if(APPLE)
        target_link_options(${target} PRIVATE "$<${NOVA_IS_RELEASE}:LINKER:-dead_strip>")
    else()
        target_link_options(${target} PRIVATE "$<${NOVA_IS_RELEASE}:LINKER:--gc-sections>")
    endif()

    if(CMAKE_GENERATOR STREQUAL "Xcode")
        foreach(config IN LISTS NOVA_RELEASE_CONFIGS)
            set_target_properties(${target} PROPERTIES
                "XCODE_ATTRIBUTE_DEPLOYMENT_POSTPROCESSING[variant=${config}]" YES
                "XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT[variant=${config}]" YES
                "XCODE_ATTRIBUTE_STRIP_STYLE[variant=${config}]" all)
        endforeach()
    elseif(APPLE AND CMAKE_STRIP)
        # Ninja/Makefiles on macOS: strip local symbols after linking (strip re-signs ad hoc).
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "$<IF:${NOVA_IS_RELEASE},${CMAKE_STRIP},true>" -x "$<TARGET_FILE:${target}>"
            VERBATIM)
    endif()
    # Android: the Gradle plugin strips release .so files itself.
endfunction()
