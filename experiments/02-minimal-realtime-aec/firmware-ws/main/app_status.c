#include "app_status.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "status_led.h"

static SemaphoreHandle_t s_lock;
static app_status_snapshot_t s_status;

void app_status_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
    memset(&s_status, 0, sizeof(s_status));
    s_status.phase = APP_PHASE_BOOTING;
    snprintf(s_status.detail, sizeof(s_status.detail), "Starting");
}

void app_status_set_phase(app_phase_t phase, const char *detail)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.phase = phase;
    snprintf(s_status.detail, sizeof(s_status.detail), "%s", detail ? detail : "");
    xSemaphoreGive(s_lock);

    status_led_set(
        phase == APP_PHASE_LISTENING ? STATUS_LED_LISTENING :
        phase == APP_PHASE_ERROR ? STATUS_LED_ERROR :
                                   STATUS_LED_OFF);
}

void app_status_set_wifi(bool connected, const char *ip_address)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.wifi_connected = connected;
    snprintf(s_status.ip_address, sizeof(s_status.ip_address), "%s",
             ip_address ? ip_address : "");
    xSemaphoreGive(s_lock);
}

void app_status_set_realtime(bool active, bool session_ready)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.realtime_active = active;
    s_status.realtime_session_ready = session_ready;
    xSemaphoreGive(s_lock);
}

void app_status_note_audio_frame(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.audio_frames++;
    xSemaphoreGive(s_lock);
}

void app_status_note_audio_read_error(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.audio_read_errors++;
    xSemaphoreGive(s_lock);
}

void app_status_note_audio_write_error(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.audio_write_errors++;
    xSemaphoreGive(s_lock);
}

void app_status_note_playback_underrun(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.playback_underruns++;
    xSemaphoreGive(s_lock);
}

void app_status_snapshot(app_status_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *snapshot = s_status;
    xSemaphoreGive(s_lock);
}

const char *app_status_phase_name(app_phase_t phase)
{
    switch (phase) {
    case APP_PHASE_BOOTING:
        return "booting";
    case APP_PHASE_WIFI_CONNECTING:
        return "wifi_connecting";
    case APP_PHASE_READY:
        return "ready";
    case APP_PHASE_CONNECTING:
        return "connecting";
    case APP_PHASE_LISTENING:
        return "listening";
    case APP_PHASE_SPEAKING:
        return "speaking";
    case APP_PHASE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}
