set(STACKCHAN_SHARED_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../firmware-ws/main")
set(STACKCHAN_ATLAS_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../tools/sprite-pipeline/characters/gameboy-bot/generated/build/sticks3-chunky")
set(STACKCHAN_ATLAS_SOURCE
    "${STACKCHAN_ATLAS_DIR}/fspp_gameboy_bot_sticks3_chunky_atlas.c")

if(NOT EXISTS "${STACKCHAN_ATLAS_SOURCE}")
    message(FATAL_ERROR
        "StickS3 atlas missing. From tools/sprite-pipeline run: "
        "python3 -B avatar_pipeline.py build "
        "--character characters/gameboy-bot/avatar.json")
endif()

idf_component_register(
    SRCS
        "app_main.cpp"
        "${STACKCHAN_SHARED_DIR}/face_sprite_sheet.c"
        "${STACKCHAN_ATLAS_SOURCE}"
    INCLUDE_DIRS
        "."
        "${STACKCHAN_SHARED_DIR}"
        "${STACKCHAN_ATLAS_DIR}"
    REQUIRES
        M5Unified
        esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror)
