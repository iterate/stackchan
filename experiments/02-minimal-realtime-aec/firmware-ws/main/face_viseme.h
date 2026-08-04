#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_driver.h"

enum {
    FACE_VISEME_MODEL_RECORD_BYTES = 368,
    FACE_VISEME_FEATURE_COUNT = 12,
};

typedef struct {
    const uint8_t *model_data;
    size_t model_size;
    uint16_t speaker_mean_hz;
    uint16_t vad_open_level;
    uint16_t vad_close_level;
    uint16_t mouth_level_range;
    uint16_t attack_ms;
    uint16_t release_ms;
    uint16_t shape_ms;
    uint8_t vad_release_hops;
    uint8_t vote_window;
    bool prime_vote_on_speech;
} face_viseme_config_t;

extern const face_viseme_config_t FACE_VISEME_DEFAULT_CONFIG;
extern const face_algorithm_t FACE_ALGORITHM_VISEME;

bool face_viseme_model_valid(const uint8_t *model_data, size_t model_size);
size_t face_viseme_model_record_count(size_t model_size);
bool face_viseme_model_phoneme(
    const uint8_t *model_data, size_t model_size, uint8_t prototype,
    uint32_t *first_codepoint, uint32_t *second_codepoint);

#ifdef STACKCHAN_HOST_TEST
bool face_viseme_test_extract_features(
    const int16_t *samples, size_t sample_count,
    uint16_t speaker_mean_hz,
    float features[FACE_VISEME_FEATURE_COUNT]);
#endif
