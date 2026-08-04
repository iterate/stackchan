#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define STACKCHAN_REALTIME_EVENT_LOG_CAPACITY 384
#define STACKCHAN_REALTIME_EVENT_DIRECTION_BYTES 4
#define STACKCHAN_REALTIME_EVENT_KIND_BYTES 20
#define STACKCHAN_REALTIME_EVENT_TYPE_BYTES 72
#define STACKCHAN_REALTIME_EVENT_TEXT_BYTES 192

typedef struct {
    uint32_t sequence;
    uint64_t timestamp_ms;
    uint32_t payload_bytes;
    uint32_t audio_samples;
    char direction[STACKCHAN_REALTIME_EVENT_DIRECTION_BYTES];
    char kind[STACKCHAN_REALTIME_EVENT_KIND_BYTES];
    char type[STACKCHAN_REALTIME_EVENT_TYPE_BYTES];
    char text[STACKCHAN_REALTIME_EVENT_TEXT_BYTES];
} realtime_event_record_t;

typedef struct {
    size_t capacity;
    size_t available;
    uint32_t newest_sequence;
    uint32_t overwritten_records;
    uint32_t lock_contention_drops;
} realtime_event_log_info_t;

esp_err_t realtime_event_log_init(void);

/*
 * Records semantic WebSocket/control events in a bounded PSRAM ring.
 * Audio payload bytes are never copied into the journal: callers provide only
 * their size and sample count.
 */
void realtime_event_log_record(const char *direction, const char *kind,
                               const char *type, const char *text,
                               size_t payload_bytes, size_t audio_samples);

/*
 * Copies an oldest-to-newest stable snapshot. This blocks only the caller;
 * producers use a zero-wait lock and may increment lock_contention_drops while
 * a snapshot is being made.
 */
size_t realtime_event_log_copy(realtime_event_record_t *destination,
                               size_t capacity,
                               realtime_event_log_info_t *info);
