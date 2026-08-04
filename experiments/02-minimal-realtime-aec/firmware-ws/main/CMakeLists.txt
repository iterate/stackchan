set(STACKCHAN_LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../local")
set(STACKCHAN_DEFAULTS_DIR "${CMAKE_CURRENT_LIST_DIR}/defaults")
file(GLOB STACKCHAN_GENERATED_AVATAR_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/fspp_*_cores3_*_atlas.c")

if(EXISTS "${STACKCHAN_LOCAL_DIR}/stackchan_local_secrets.h")
    set(STACKCHAN_SECRETS_DIR "${STACKCHAN_LOCAL_DIR}")
    message(STATUS "StackChan: using gitignored local credentials")
else()
    set(STACKCHAN_SECRETS_DIR "${STACKCHAN_DEFAULTS_DIR}")
    message(WARNING "StackChan: local credentials missing; firmware will build but Wi-Fi and Realtime are disabled")
endif()

idf_component_register(
    SRCS
        "app_main.c"
        "app_status.c"
        "audio_pipeline.c"
        "audio_trace.c"
        "debug_log.c"
        "diagnostics.c"
        "face_animator.c"
        "face_avatar_registry.c"
        "face_driver.c"
        "face_geometry.c"
        "face_keyframe.c"
        "face_performance.c"
        "face_sprite_sheet.c"
        "face_sprite_mossling_generated.c"
        "face_sprite_showcase.c"
        ${STACKCHAN_GENERATED_AVATAR_SRCS}
        "face_stage.c"
        "face_spectral.c"
        "face_viseme.c"
        "realtime_client.c"
        "realtime_event_log.c"
        "speech_leveler.c"
        "status_led.c"
        "ui.c"
        "web_control.c"
        "wifi_station.c"
    INCLUDE_DIRS
        "."
        "${STACKCHAN_SECRETS_DIR}"
        "${STACKCHAN_DEFAULTS_DIR}"
    REQUIRES
        esp_event
        esp_app_format
        esp_http_server
        esp_netif
        esp_timer
        esp_wifi
        json
        mbedtls
        mdns
        nvs_flash
        espressif__esp_asrc
        espressif__esp-sr
        espressif__esp_websocket_client
        espressif__m5stack_core_s3
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror=return-type
    -Werror=implicit-function-declaration
)
