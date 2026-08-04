#pragma once

#include <stddef.h>
#include <stdint.h>

#include "face_driver.h"
#include "face_geometry.h"

typedef struct stackchan_face_host stackchan_face_host_t;

size_t stackchan_face_animator_size(void);
size_t stackchan_face_state_size(void);
size_t stackchan_face_geometry_size(void);
size_t stackchan_face_algorithm_state_size(const char *algorithm);
stackchan_face_host_t *stackchan_face_host_create(uint32_t sample_rate);
stackchan_face_host_t *stackchan_face_host_create_algorithm(
    const char *algorithm,
    uint32_t sample_rate,
    const void *config,
    size_t config_size);
void stackchan_face_host_destroy(stackchan_face_host_t *host);
const char *stackchan_face_host_algorithm_name(
    const stackchan_face_host_t *host);
size_t stackchan_face_host_algorithm_state_size(
    const stackchan_face_host_t *host);
void stackchan_face_host_push_pcm(stackchan_face_host_t *host,
                                  const int16_t *samples,
                                  size_t sample_count);
void stackchan_face_host_push_event(
    stackchan_face_host_t *host,
    const face_stream_event_t *event);
void stackchan_face_host_snapshot(const stackchan_face_host_t *host,
                                  uint16_t display_width,
                                  uint16_t display_height,
                                  face_pose_t *state,
                                  face_geometry_t *geometry);
