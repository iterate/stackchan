#include "face_wasm_bridge.h"

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "face_animator.h"
#include "face_host_bridge.h"
#include "face_spectral.h"
#include "face_viseme.h"

static const face_envelope_config_t ENVELOPE_FAST_CONFIG = {
    .speech_floor = 256,
    .mouth_dynamic_range = 4600,
    .attack_percent = 75,
    .release_percent = 25,
};

static const face_envelope_config_t ENVELOPE_SMOOTH_CONFIG = {
    .speech_floor = 256,
    .mouth_dynamic_range = 4600,
    .attack_percent = 38,
    .release_percent = 10,
};

static const face_spectral_config_t SPECTRAL_RESPONSIVE_CONFIG = {
    .speech_floor = 220,
    .mouth_dynamic_range = 4400,
    .attack_percent = 72,
    .release_percent = 22,
    .shape_percent = 58,
    .fricative_percent = 34,
};

static const face_spectral_config_t SPECTRAL_SMOOTH_CONFIG = {
    .speech_floor = 220,
    .mouth_dynamic_range = 4400,
    .attack_percent = 45,
    .release_percent = 13,
    .shape_percent = 32,
    .fricative_percent = 34,
};

typedef struct {
    stackchan_face_host_t *host;
    uint16_t model_bytes;
} stackchan_wasm_instance_t;

static face_viseme_config_t viseme_config(
    uint32_t profile,
    const uint8_t *model,
    size_t model_bytes)
{
    face_viseme_config_t config = {
        .model_data = model,
        .model_size = model_bytes,
        .speaker_mean_hz = 150,
        .vad_open_level = 36,
        .vad_close_level = 18,
        .mouth_level_range = 3000,
        .attack_ms = 35,
        .release_ms = 70,
        .shape_ms = 45,
        .vad_release_hops = 4,
        .vote_window = 3,
        .prime_vote_on_speech = true,
    };
    if (profile == STACKCHAN_WASM_PROFILE_VISEME_BALANCED) {
        config.attack_ms = 55;
        config.release_ms = 110;
        config.shape_ms = 90;
        config.vote_window = 5;
    }
    return config;
}

uint32_t stackchan_wasm_abi_version(void)
{
    return STACKCHAN_WASM_ABI_VERSION;
}

size_t stackchan_wasm_keyframe_size(void)
{
    return sizeof(face_keyframe_t);
}

size_t stackchan_wasm_render_key_size(void)
{
    return sizeof(face_render_key_t);
}

size_t stackchan_wasm_metrics_size(void)
{
    return sizeof(stackchan_wasm_metrics_t);
}

size_t stackchan_wasm_render_frame_bytes(void)
{
    return FACE_RENDER_FRAME_BYTES;
}

size_t stackchan_wasm_stage_cue_size(void)
{
    return sizeof(face_stage_cue_t);
}

size_t stackchan_wasm_render_profile_count(void)
{
    return face_render_profile_count();
}

const char *stackchan_wasm_render_profile_slug(uint32_t profile)
{
    return face_render_profile_slug((face_render_profile_t)profile);
}

const char *stackchan_wasm_render_profile_name(uint32_t profile)
{
    return face_render_profile_name((face_render_profile_t)profile);
}

const char *stackchan_wasm_render_profile_family_name(uint32_t profile)
{
    return face_render_profile_family_name(
        (face_render_profile_t)profile);
}

int stackchan_wasm_render_profile_info(
    uint32_t profile, face_render_info_t *info)
{
    return face_render_profile_info(
               (face_render_profile_t)profile, info)
               ? 1
               : 0;
}

void *stackchan_wasm_create(
    uint32_t profile,
    uint32_t sample_rate,
    const uint8_t *viseme_model,
    size_t viseme_model_bytes)
{
    if (sample_rate == 0) {
        return NULL;
    }
    stackchan_wasm_instance_t *instance =
        calloc(1, sizeof(*instance));
    if (instance == NULL) {
        return NULL;
    }
    switch (profile) {
    case STACKCHAN_WASM_PROFILE_ENVELOPE_FAST:
        instance->host = stackchan_face_host_create_algorithm(
            "envelope", sample_rate,
            &ENVELOPE_FAST_CONFIG, sizeof(ENVELOPE_FAST_CONFIG));
        break;
    case STACKCHAN_WASM_PROFILE_ENVELOPE_SMOOTH:
        instance->host = stackchan_face_host_create_algorithm(
            "envelope", sample_rate,
            &ENVELOPE_SMOOTH_CONFIG, sizeof(ENVELOPE_SMOOTH_CONFIG));
        break;
    case STACKCHAN_WASM_PROFILE_VISEME_RESPONSIVE:
    case STACKCHAN_WASM_PROFILE_VISEME_BALANCED: {
        const face_viseme_config_t config =
            viseme_config(profile, viseme_model, viseme_model_bytes);
        instance->host = stackchan_face_host_create_algorithm(
            "viseme", sample_rate, &config, sizeof(config));
        instance->model_bytes = (uint16_t)(
            viseme_model_bytes > UINT16_MAX
                ? UINT16_MAX
                : viseme_model_bytes);
        break;
    }
    case STACKCHAN_WASM_PROFILE_SPECTRAL_RESPONSIVE:
        instance->host = stackchan_face_host_create_algorithm(
            "spectral", sample_rate,
            &SPECTRAL_RESPONSIVE_CONFIG,
            sizeof(SPECTRAL_RESPONSIVE_CONFIG));
        break;
    case STACKCHAN_WASM_PROFILE_SPECTRAL_SMOOTH:
        instance->host = stackchan_face_host_create_algorithm(
            "spectral", sample_rate,
            &SPECTRAL_SMOOTH_CONFIG,
            sizeof(SPECTRAL_SMOOTH_CONFIG));
        break;
    default:
        free(instance);
        return NULL;
    }
    if (instance->host == NULL) {
        free(instance);
        return NULL;
    }
    return instance;
}

void stackchan_wasm_destroy(void *instance)
{
    stackchan_wasm_instance_t *wasm_instance = instance;
    if (wasm_instance == NULL) {
        return;
    }
    stackchan_face_host_destroy(wasm_instance->host);
    free(wasm_instance);
}

void stackchan_wasm_push_pcm(
    void *instance, const int16_t *samples, size_t sample_count)
{
    stackchan_wasm_instance_t *wasm_instance = instance;
    if (wasm_instance == NULL) {
        return;
    }
    stackchan_face_host_push_pcm(
        wasm_instance->host, samples, sample_count);
}

int stackchan_wasm_snapshot(
    const void *instance,
    face_render_key_t *render_key,
    stackchan_wasm_metrics_t *metrics)
{
    if (instance == NULL || render_key == NULL || metrics == NULL) {
        return 0;
    }
    const stackchan_wasm_instance_t *wasm_instance = instance;
    face_pose_t pose;
    face_geometry_t geometry;
    memset(&pose, 0, sizeof(pose));
    memset(&geometry, 0, sizeof(geometry));
    stackchan_face_host_snapshot(
        wasm_instance->host, 320, 240, &pose, &geometry);
    face_render_key_from_pose(&pose, render_key);
    metrics->frame_index = pose.frame_index;
    metrics->playout_samples = pose.playout_samples;
    metrics->level = pose.level;
    metrics->viseme = pose.viseme;
    metrics->confidence = pose.confidence;
    const size_t state_bytes =
        stackchan_face_host_algorithm_state_size(wasm_instance->host);
    metrics->state_bytes = (uint16_t)(
        state_bytes > UINT16_MAX ? UINT16_MAX : state_bytes);
    metrics->model_bytes = wasm_instance->model_bytes;
    return 1;
}

int stackchan_wasm_render(
    uint32_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    return face_render_frame(
               (face_render_profile_t)profile,
               render_key,
               sample_clock,
               rgb565,
               pixel_capacity)
               ? 1
               : 0;
}

int stackchan_wasm_apply_stage_cue(
    const face_stage_cue_t *cue,
    uint32_t sample_clock,
    face_render_key_t *render_key)
{
    return face_stage_cue_apply(cue, sample_clock, render_key) ? 1 : 0;
}
