# Stages a game's assets into its bundle (or an Android assets folder). Run with cmake -P:
#
#   -DPACK=0|1   1: DEST/game.pak, encrypted (release); 0: a plain copy of the asset tree
#   -DSRC=<dir>  the game's assets folder
#   -DDEST=<dir> bundle Resources folder (or Android pack folder)
#   -DTOOL=<nova_pack> -DKEYFILE=<key file>   (pack mode)
#
# DEST/.nova-staged lists what the previous run staged; those files are removed first, so
# switching between modes (or deleting an asset) never leaves stale files in the bundle.
cmake_minimum_required(VERSION 3.24)

foreach(var SRC DEST)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "NovaStageAssets: -D${var}= is required")
    endif()
endforeach()
set(manifest "${DEST}/.nova-staged")

if(EXISTS "${manifest}")
    file(STRINGS "${manifest}" previous)
    set(dirs "")
    foreach(rel IN LISTS previous)
        file(REMOVE "${DEST}/${rel}")
        get_filename_component(dir "${rel}" DIRECTORY)
        while(dir)
            list(APPEND dirs "${dir}")
            get_filename_component(dir "${dir}" DIRECTORY)
        endwhile()
    endforeach()
    # Remove folders the staged files left empty, deepest first.
    list(REMOVE_DUPLICATES dirs)
    list(SORT dirs ORDER DESCENDING)
    foreach(dir IN LISTS dirs)
        file(GLOB left "${DEST}/${dir}/*")
        file(GLOB hidden "${DEST}/${dir}/.*")
        if(NOT left AND NOT hidden)
            file(REMOVE_RECURSE "${DEST}/${dir}")
        endif()
    endforeach()
    file(REMOVE "${manifest}")
endif()
file(MAKE_DIRECTORY "${DEST}")

if(PACK)
    execute_process(COMMAND "${TOOL}" pack "${SRC}" "${DEST}/game.pak" --key-file "${KEYFILE}"
                    RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "NovaStageAssets: nova_pack failed (${rc})")
    endif()
    file(WRITE "${manifest}" "game.pak\n")
else()
    file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${SRC}" "${SRC}/*")
    set(staged "")
    foreach(rel IN LISTS files)
        # Skip dot-files and anything inside dot-folders, like nova_pack does.
        if(rel MATCHES "(^|/)\\.")
            continue()
        endif()
        get_filename_component(dir "${rel}" DIRECTORY)
        file(COPY "${SRC}/${rel}" DESTINATION "${DEST}/${dir}")
        string(APPEND staged "${rel}\n")
    endforeach()
    file(WRITE "${manifest}" "${staged}")
endif()
