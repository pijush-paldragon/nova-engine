# Stages a game's assets into its bundle (or an Android assets folder). Run with cmake -P:
#
#   -DPACK=0|1   1: DEST/game.pak, encrypted (release); 0: a plain copy of the asset tree
#   -DSRC=<dir>  the game's assets folder
#   -DDEST=<dir> bundle Resources folder (or Android pack folder); XCODE: the resources folder of
#                the bundle Xcode is building ($TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH)
#   -DTOOL=<nova_pack> -DKEYFILE=<key file>   (pack mode)
#   -DASSET_LIST=<hash>  ignored here; it changes the command when files are added or renamed
#
# DEST/.nova-staged lists what the previous run staged; those files are removed first, so
# switching between modes (or deleting an asset) never leaves stale files in the bundle.
cmake_minimum_required(VERSION 3.24)

foreach(var SRC DEST)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "NovaStageAssets: -D${var}= is required")
    endif()
endforeach()
if(DEST STREQUAL "XCODE")
    if(NOT DEFINED ENV{TARGET_BUILD_DIR} OR NOT DEFINED ENV{UNLOCALIZED_RESOURCES_FOLDER_PATH})
        message(FATAL_ERROR "NovaStageAssets: DEST=XCODE outside an Xcode build phase")
    endif()
    set(DEST "$ENV{TARGET_BUILD_DIR}/$ENV{UNLOCALIZED_RESOURCES_FOLDER_PATH}")
endif()
set(manifest "${DEST}/.nova-staged")

# Removes DEST/<rel> for each path, then the folders that left empty (deepest first).
function(unstage)
    set(dirs "")
    foreach(rel IN LISTS ARGN)
        file(REMOVE "${DEST}/${rel}")
        get_filename_component(dir "${rel}" DIRECTORY)
        while(dir)
            list(APPEND dirs "${dir}")
            get_filename_component(dir "${dir}" DIRECTORY)
        endwhile()
    endforeach()
    list(REMOVE_DUPLICATES dirs)
    list(SORT dirs ORDER DESCENDING)
    foreach(dir IN LISTS dirs)
        file(GLOB left "${DEST}/${dir}/*")
        file(GLOB hidden "${DEST}/${dir}/.*")
        if(IS_DIRECTORY "${DEST}/${dir}" AND NOT left AND NOT hidden)
            file(REMOVE_RECURSE "${DEST}/${dir}")
        endif()
    endforeach()
endfunction()

if(EXISTS "${manifest}")
    file(STRINGS "${manifest}" previous ENCODING UTF-8)
    unstage(${previous})
    file(REMOVE "${manifest}")
endif()
file(MAKE_DIRECTORY "${DEST}")

# Nothing in the build may swap the key: KEYFILE is the key resolved at configure time, the one
# compiled into the game, and nova_pack would otherwise prefer $NOVA_PACK_KEY.
unset(ENV{NOVA_PACK_KEY})

file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${SRC}" "${SRC}/*")
list(FILTER files EXCLUDE REGEX "(^|/)\\.") # dot-files and dot-folders, like nova_pack

if(PACK)
    # Loose copies with no manifest (bundles from before staging) must not ship next to the pack.
    unstage(${files})
    execute_process(COMMAND "${TOOL}" pack "${SRC}" "${DEST}/game.pak" --key-file "${KEYFILE}"
                    RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "NovaStageAssets: nova_pack failed (${rc})")
    endif()
    file(WRITE "${manifest}" "game.pak\n")
else()
    set(staged "")
    foreach(rel IN LISTS files)
        get_filename_component(dir "${rel}" DIRECTORY)
        file(COPY "${SRC}/${rel}" DESTINATION "${DEST}/${dir}")
        string(APPEND staged "${rel}\n")
    endforeach()
    file(WRITE "${manifest}" "${staged}")
endif()
