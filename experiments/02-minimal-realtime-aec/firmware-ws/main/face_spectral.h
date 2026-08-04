#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_driver.h"

enum {
    FACE_SPECTRAL_SAMPLE_RATE = 16000,
    FACE_SPECTRAL_BAND_COUNT = 7,
};

/*
 * Integer-only, streaming spectral face driver. It keeps one Goertzel
 * recurrence per band: no PCM window, FFT workspace, model, or heap.
 */
typedef struct {
    uint16_t speech_floor;
    uint16_t mouth_dynamic_range;
    uint8_t attack_percent;
    uint8_t release_percent;
    uint8_t shape_percent;
    uint8_t fricative_percent;
} face_spectral_config_t;

typedef struct {
    uint32_t window_fill;
    uint32_t sum_abs;
    uint32_t published_sequence;
    uint32_t next_blink_frame;
    uint32_t next_gaze_frame;
    int32_t recurrence_1[FACE_SPECTRAL_BAND_COUNT];
    int32_t recurrence_2[FACE_SPECTRAL_BAND_COUNT];
    face_pose_t pose;
    face_spectral_config_t config;
    uint8_t blink_phase;
} face_spectral_state_t;

extern const face_spectral_config_t FACE_SPECTRAL_DEFAULT_CONFIG;
extern const face_algorithm_t FACE_ALGORITHM_SPECTRAL;

