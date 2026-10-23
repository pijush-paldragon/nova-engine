# Builds tools/nova_pack for the build machine, even when the game is configured for iOS or
# Android: a separate CMake project with no toolchain file and none of the CMAKE_OSX_* settings.
#
# Sets:
#   NOVA_PACK_TOOL          path of the nova_pack executable
#   NOVA_PACK_TOOL_SOURCES  its sources (DEPENDS for commands that run it)
#   NOVA_PACK_TOOL_UPDATE   a COMMAND that rebuilds it if a source changed since configure
include_guard(GLOBAL)

set(_nova_host_dir "${CMAKE_BINARY_DIR}/nova-host")
set(_nova_tool_src "${NOVA_ENGINE_DIR}/tools/nova_pack")

# A host build left by an engine at another path can't be reused (CMake refuses a new source dir).
if(EXISTS "${_nova_host_dir}/CMakeCache.txt")
    file(STRINGS "${_nova_host_dir}/CMakeCache.txt" _nova_host_home REGEX "^CMAKE_HOME_DIRECTORY:INTERNAL=")
    string(REPLACE "CMAKE_HOME_DIRECTORY:INTERNAL=" "" _nova_host_home "${_nova_host_home}")
    get_filename_component(_nova_host_home "${_nova_host_home}" REALPATH)
    if(NOT _nova_host_home STREQUAL _nova_tool_src)
        file(REMOVE_RECURSE "${_nova_host_dir}")
    endif()
endif()

# Single-config generators put the tool at a known path; anything else uses Unix Makefiles.
if(CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_GENERATOR MATCHES "Makefiles")
    set(_nova_host_gen -G "${CMAKE_GENERATOR}")
    if(CMAKE_MAKE_PROGRAM)
        list(APPEND _nova_host_gen "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
    endif()
else()
    set(_nova_host_gen -G "Unix Makefiles")
endif()

# Xcode build phases export SDKROOT and *_DEPLOYMENT_TARGET, which clang obeys; rebuilding the
# tool from an iOS build phase must not retarget it, so they are cleared and macOS is pinned.
set(_nova_host_env "${CMAKE_COMMAND}" -E env --unset=SDKROOT --unset=IPHONEOS_DEPLOYMENT_TARGET
    --unset=TVOS_DEPLOYMENT_TARGET --unset=WATCHOS_DEPLOYMENT_TARGET --unset=XROS_DEPLOYMENT_TARGET)
set(_nova_host_platform "")
if(CMAKE_HOST_APPLE)
    set(_nova_host_platform -DCMAKE_OSX_SYSROOT=macosx)
endif()

execute_process(
    COMMAND ${_nova_host_env} "${CMAKE_COMMAND}" -S "${_nova_tool_src}" -B "${_nova_host_dir}" ${_nova_host_gen}
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE= ${_nova_host_platform}
    RESULT_VARIABLE _nova_host_result OUTPUT_VARIABLE _nova_host_out ERROR_VARIABLE _nova_host_out)
if(_nova_host_result EQUAL 0)
    execute_process(COMMAND ${_nova_host_env} "${CMAKE_COMMAND}" --build "${_nova_host_dir}"
        RESULT_VARIABLE _nova_host_result OUTPUT_VARIABLE _nova_host_out ERROR_VARIABLE _nova_host_out)
endif()
if(NOT _nova_host_result EQUAL 0)
    message(FATAL_ERROR "Building the nova_pack host tool failed:\n${_nova_host_out}")
endif()

set(NOVA_PACK_TOOL "${_nova_host_dir}/nova_pack")
set(NOVA_PACK_TOOL_SOURCES
    "${_nova_tool_src}/main.cpp"
    "${_nova_tool_src}/CMakeLists.txt"
    "${NOVA_ENGINE_DIR}/engine/src/platform/pack_format.cpp"
    "${NOVA_ENGINE_DIR}/engine/src/platform/pack_format.hpp")
set(NOVA_PACK_TOOL_UPDATE ${_nova_host_env} "${CMAKE_COMMAND}" --build "${_nova_host_dir}")
message(STATUS "nova_pack: ${NOVA_PACK_TOOL}")

# Resolves the pack key of the game in the current source dir and generates the C++ file that
# embeds it (split into XOR shares, see nova_pack embed-key). Key order: $NOVA_PACK_KEY, then
# <game>/.nova/pack.key, else a new key is generated there.
#   <src_var>      the generated nova_pack_key.cpp, to compile into the game
#   <keyfile_var>  a key file for packing at build time (Xcode/Gradle builds don't see the shell env)
function(nova_pack_key src_var keyfile_var)
    set(src "${CMAKE_CURRENT_BINARY_DIR}/nova_pack_key.cpp")
    set(env_key "$ENV{NOVA_PACK_KEY}")
    if(env_key)
        string(LENGTH "${env_key}" len)
        if(NOT len EQUAL 64 OR NOT env_key MATCHES "^[0-9a-fA-F]+$")
            message(FATAL_ERROR "NOVA_PACK_KEY must be 64 hex characters")
        endif()
        set(keyfile "${CMAKE_CURRENT_BINARY_DIR}/.nova/pack.key")
        file(WRITE "${keyfile}" "${env_key}\n")
    else()
        set(keyfile "${CMAKE_CURRENT_SOURCE_DIR}/.nova/pack.key")
        if(NOT EXISTS "${keyfile}")
            execute_process(COMMAND "${NOVA_PACK_TOOL}" keygen "${keyfile}" RESULT_VARIABLE rc ERROR_VARIABLE err)
            if(NOT rc EQUAL 0)
                message(FATAL_ERROR "nova_pack keygen failed: ${err}")
            endif()
            message(WARNING "Created a new asset pack key: ${keyfile}\n"
                "Back it up somewhere safe (it is git-ignored). Released apps can only read packs made "
                "with this key, so future content packs need it.")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${keyfile}")
    endif()

    # Regenerate only when the key changes, so a reconfigure doesn't rebuild the game.
    file(READ "${keyfile}" key_text)
    string(STRIP "${key_text}" key_text)
    string(SHA256 key_hash "${key_text}")
    set(stamp "${src}.stamp")
    set(old_hash "")
    if(EXISTS "${stamp}" AND EXISTS "${src}")
        file(READ "${stamp}" old_hash)
    endif()
    if(NOT old_hash STREQUAL key_hash)
        execute_process(COMMAND "${NOVA_PACK_TOOL}" embed-key "${keyfile}" "${src}" RESULT_VARIABLE rc ERROR_VARIABLE err)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "nova_pack embed-key failed: ${err}")
        endif()
        file(WRITE "${stamp}" "${key_hash}")
    endif()
    set(${src_var} "${src}" PARENT_SCOPE)
    set(${keyfile_var} "${keyfile}" PARENT_SCOPE)
endfunction()
