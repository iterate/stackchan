#include <stdio.h>

#include "app_status.h"
#include "audio_pipeline.h"
#include "debug_log.h"
#include "diagnostics.h"
#include "esp_check.h"
#include "esp_log.h"
#include "realtime_client.h"
#include "ui.h"
#include "web_control.h"
#include "wifi_station.h"

static const char *TAG = "stackchan";
static char s_ip_address[16];

static esp_err_t conversation_start(void *context)
{
    (void)context;
    return realtime_client_is_streaming() ? ESP_OK :
                                            ESP_ERR_INVALID_STATE;
}

static esp_err_t conversation_stop(void *context)
{
    (void)context;
    return realtime_client_cancel();
}

static void screen_tapped(void)
{
    (void)realtime_client_cancel();
}

void app_main(void)
{
    app_status_init();
    diagnostics_init();
    ESP_ERROR_CHECK_WITHOUT_ABORT(debug_log_init());

    ESP_ERROR_CHECK(ui_init(screen_tapped));

    app_status_set_phase(APP_PHASE_WIFI_CONNECTING, "Connecting to Wi-Fi");
    ui_set_status("Connecting", "Joining Wi-Fi...");
    esp_err_t result =
        wifi_station_connect(30000, s_ip_address, sizeof(s_ip_address));
    if (result != ESP_OK) {
        app_status_set_phase(APP_PHASE_ERROR, "Wi-Fi connection failed");
        ui_set_status("Wi-Fi error", esp_err_to_name(result));
        ESP_LOGE(TAG, "Wi-Fi connection failed: %s", esp_err_to_name(result));
        return;
    }
    app_status_set_wifi(true, s_ip_address);

    const web_control_callbacks_t callbacks = {
        .start_conversation = conversation_start,
        .stop_conversation = conversation_stop,
    };
    result = web_control_start(&callbacks);
    if (result != ESP_OK) {
        app_status_set_phase(APP_PHASE_ERROR, "HTTP control failed");
        ui_set_status("Control error", esp_err_to_name(result));
        ESP_LOGE(TAG, "HTTP control failed: %s", esp_err_to_name(result));
        return;
    }

    char detail[96];
    snprintf(
        detail, sizeof(detail), "Diagnostics online: http://%s",
        s_ip_address);
    app_status_set_phase(APP_PHASE_BOOTING, detail);
    ui_set_status("Starting audio", detail);
    ESP_LOGI(TAG, "%s", detail);

    result = audio_pipeline_init();
    if (result != ESP_OK) {
        char error_detail[96];
        snprintf(
            error_detail, sizeof(error_detail),
            "Audio initialization failed: %s", esp_err_to_name(result));
        app_status_set_phase(APP_PHASE_ERROR, error_detail);
        ui_set_status("Audio error", error_detail);
        ESP_LOGE(TAG, "%s", error_detail);
        return;
    }

    result = realtime_client_init();
    if (result != ESP_OK) {
        char error_detail[96];
        snprintf(
            error_detail, sizeof(error_detail),
            "Realtime initialization failed: %s", esp_err_to_name(result));
        app_status_set_phase(APP_PHASE_ERROR, error_detail);
        ui_set_status("Realtime error", error_detail);
        ESP_LOGE(TAG, "%s", error_detail);
        return;
    }

    snprintf(
        detail, sizeof(detail), "Realtime connecting; diagnostics: http://%s",
        s_ip_address);
    ESP_LOGI(TAG, "%s", detail);
}
