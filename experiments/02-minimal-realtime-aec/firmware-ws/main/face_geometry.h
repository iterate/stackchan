#pragma once

#include <stdint.h>

#include "face_pose.h"

/*
 * Pixel geometry shared by the embedded LVGL view and the host simulator.
 * Coordinates are top-left origins; offsets are relative to each eye centre.
 */
typedef struct {
    int16_t left_eye_x;
    int16_t left_eye_y;
    int16_t right_eye_x;
    int16_t right_eye_y;
    uint16_t eye_width;
    uint16_t eye_height;
    uint16_t pupil_size;
    int16_t pupil_offset_x;
    int16_t pupil_offset_y;
    int16_t mouth_x;
    int16_t mouth_y;
    uint16_t mouth_width;
    uint16_t mouth_height;
} face_geometry_t;

void face_geometry_from_state(const face_animator_state_t *state,
                              uint16_t display_width,
                              uint16_t display_height,
                              face_geometry_t *geometry);
