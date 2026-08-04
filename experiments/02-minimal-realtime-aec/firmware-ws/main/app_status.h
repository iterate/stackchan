#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    APP_PHASE_BOOTING = 0,
    APP_PHASE_WIFI_CONNECTING,
    APP_PHASE_READY,
    APP_PHASE_CONNECTING,
    APP_PHASE_LISTENING,
    APP_PHASE_SPEAKING,
    APP_PHASE_ERROR,
} app_phase_t;

typedef struct {
    app_phase_t phase;
    bool wifi_connected;
    bool realtime_active;
    bool realtime_session_ready;
    char ip_address[16];
    char detail[96];
    uint32_t audio_frames;
    uint32_t audio_read_errors;
    uint32_t audio_write_errors;
    uint32_t playback_underruns;
} app_status_snapshot_t;

void app_status_init(void);
void app_status_set_phase(app_phase_t phase, const char *detail);
void app_status_set_wifi(bool connected, const char *ip_address);
void app_status_set_realtime(bool active, bool session_ready);
void app_status_note_audio_frame(void);
void app_status_note_audio_read_error(void);
void app_status_note_audio_write_error(void);
void app_status_note_playback_underrun(void);
void app_status_snapshot(app_status_snapshot_t *snapshot);
const char *app_status_phase_name(app_phase_t phase);
