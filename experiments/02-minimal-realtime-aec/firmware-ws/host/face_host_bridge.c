#include "face_host_bridge.h"

#include <stdlib.h>
#include <string.h>

#include "face_animator.h"
#include "face_spectral.h"
#include "face_viseme.h"

struct stackchan_face_host {
    face_driver_t driver;
    void *state;
};

static const face_algorithm_t *find_algorithm(const char *name)
{
    if (name == NULL || strcmp(name, FACE_ALGORITHM_ENVELOPE.name) == 0) {
        return &FACE_ALGORITHM_ENVELOPE;
    }
    if (strcmp(name, FACE_ALGORITHM_VISEME.name) == 0) {
        return &FACE_ALGORITHM_VISEME;
    }
    if (strcmp(name, FACE_ALGORITHM_SPECTRAL.name) == 0) {
        return &FACE_ALGORITHM_SPECTRAL;
    }
    return NULL;
}

size_t stackchan_face_animator_size(void)
{
    return sizeof(face_animator_t);
}

size_t stackchan_face_state_size(void)
{
    return sizeof(face_animator_state_t);
}

size_t stackchan_face_geometry_size(void)
{
    return sizeof(face_geometry_t);
}

size_t stackchan_face_algorithm_state_size(const char *algorithm)
{
    const face_algorithm_t *descriptor = find_algorithm(algorithm);
    return descriptor == NULL ? 0 : descriptor->state_size;
}

stackchan_face_host_t *stackchan_face_host_create(uint32_t sample_rate)
{
    return stackchan_face_host_create_algorithm(
        FACE_ALGORITHM_ENVELOPE.name, sample_rate, NULL, 0);
}

stackchan_face_host_t *stackchan_face_host_create_algorithm(
    const char *algorithm,
    uint32_t sample_rate,
    const void *config,
    size_t config_size)
{
    const face_algorithm_t *descriptor = find_algorithm(algorithm);
    if (descriptor == NULL) {
        return NULL;
    }
    stackchan_face_host_t *host = calloc(1, sizeof(*host));
    if (host == NULL) {
        return NULL;
    }
    host->state = calloc(1, descriptor->state_size);
    if (host->state == NULL ||
        !face_driver_init(
            &host->driver, descriptor, host->state,
            descriptor->state_size, sample_rate, config, config_size)) {
        free(host->state);
        free(host);
        return NULL;
    }
    return host;
}

void stackchan_face_host_destroy(stackchan_face_host_t *host)
{
    if (host == NULL) {
        return;
    }
    free(host->state);
    free(host);
}

const char *stackchan_face_host_algorithm_name(
    const stackchan_face_host_t *host)
{
    return host == NULL ? "none" : face_driver_name(&host->driver);
}

size_t stackchan_face_host_algorithm_state_size(
    const stackchan_face_host_t *host)
{
    return host == NULL
               ? 0
               : face_driver_state_size(&host->driver);
}

void stackchan_face_host_push_pcm(stackchan_face_host_t *host,
                                  const int16_t *samples,
                                  size_t sample_count)
{
    if (host == NULL) {
        return;
    }
    face_driver_push_pcm(&host->driver, samples, sample_count);
}

void stackchan_face_host_push_event(
    stackchan_face_host_t *host,
    const face_stream_event_t *event)
{
    if (host == NULL) {
        return;
    }
    face_driver_push_event(&host->driver, event);
}

void stackchan_face_host_snapshot(const stackchan_face_host_t *host,
                                  uint16_t display_width,
                                  uint16_t display_height,
                                  face_pose_t *state,
                                  face_geometry_t *geometry)
{
    if (host == NULL || state == NULL || geometry == NULL) {
        return;
    }
    face_driver_snapshot(&host->driver, state);
    face_geometry_from_state(
        state, display_width, display_height, geometry);
}
