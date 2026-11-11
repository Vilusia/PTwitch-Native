include("${CMAKE_CURRENT_LIST_DIR}/../upstream/komi-tube/native/native.cmake")
if(NOT PSP OR NOT PSP_BROWSER_LIBCURL_TRANSPORT OR PSP_BROWSER_CURL_STUB)
    return()
endif()
# Temporary Build 13 diagnosis: capture two bounded startup AVC access units
# so the PSP-refused TS stream can be compared with a working fMP4 stream.
target_compile_definitions(tilefinch_core PRIVATE PTWITCH_AVC_SNAPSHOT_ENABLED=1)
list(REMOVE_AT KOMI_RUNTIME_SOURCES 0)
list(PREPEND KOMI_RUNTIME_SOURCES "${CMAKE_CURRENT_LIST_DIR}/common/komi_runtime.c")
komi_program(ptwitch-app "PTwitch Native"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/app/main.c"
        "${CMAKE_CURRENT_LIST_DIR}/app/twitch.c"
        "${CMAKE_CURRENT_LIST_DIR}/app/osk.c"
        "${CMAKE_CURRENT_LIST_DIR}/app/ui.c"
        "${CMAKE_CURRENT_LIST_DIR}/common/ptwitch_avc_snapshot.c"
    FILES
        "fonts/TilefinchSans-Regular.ttf=${CMAKE_CURRENT_SOURCE_DIR}/fonts/TilefinchSans-Regular.ttf"
        "fonts/LICENSE-TilefinchSans.txt=${CMAKE_CURRENT_SOURCE_DIR}/fonts/LICENSE-TilefinchSans.txt"
        "fonts/LICENSE-Unifont.txt=${CMAKE_CURRENT_SOURCE_DIR}/fonts/LICENSE-Unifont.txt")
