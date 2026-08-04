#pragma once

#include "fea_favourite_variants.h"
#include "fea_internal.h"

typedef struct {
    fea_pt_t anchor;
    fea_pt_t eye[2];
    fea_pt_t pupil[2];
    int32_t eye_rx_q4;
    int32_t eye_ry_q4[2];
    int32_t pupil_r_q4;
    int32_t open_q8[2];
    fea_pt_t brow_in[2];
    fea_pt_t brow_out[2];
    fea_lipmouth_t mouth;
} fea_favourite_layout_t;

typedef enum {
    FEA_FAVOURITE_EYE_EMBER = 0,
    FEA_FAVOURITE_EYE_LENS,
    FEA_FAVOURITE_EYE_INK,
    FEA_FAVOURITE_EYE_BUTTON,
} fea_favourite_eye_kind_t;

typedef struct {
    fea_favourite_eye_kind_t kind;
    uint16_t socket;
    uint16_t white;
    uint16_t iris;
    uint16_t pupil;
    uint16_t glint;
    uint16_t lid;
    uint16_t lid_fill;
    uint8_t outlined;
    uint8_t happy_arcs;
} fea_favourite_eye_style_t;

void fea_favourite_layout_build(
    fea_favourite_profile_t profile,
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_favourite_layout_t *layout);

void fea_favourite_layout_probe(
    fea_favourite_profile_t profile,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    fea_probe_t *probe);

void fea_favourite_set_mouth_colors(
    fea_favourite_layout_t *layout,
    uint16_t lip,
    uint16_t fill,
    uint16_t teeth,
    uint16_t tongue,
    int32_t lip_extra_q4);

void fea_favourite_draw_eyes(
    fea_canvas_t *canvas,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    const fea_favourite_eye_style_t *style);

void fea_favourite_draw_brows(
    fea_canvas_t *canvas,
    const fea_favourite_layout_t *layout,
    uint16_t color,
    int32_t thickness_q4,
    uint32_t alpha);

void fea_favourite_draw_stars(
    fea_canvas_t *canvas,
    uint32_t seed,
    uint16_t color,
    uint32_t count,
    uint32_t max_alpha);

void fea_favourite_draw_spark(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    int32_t radius_q4,
    int32_t thickness_q4,
    uint16_t color,
    uint32_t alpha);

void fea_favourite_draw_heart(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    int32_t size_q4,
    uint16_t color,
    uint32_t alpha);

void fea_favourite_draw_drop(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    int32_t size_q4,
    uint16_t color,
    uint32_t alpha);

void fea_favourite_draw_z(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    int32_t size_q4,
    int32_t thickness_q4,
    uint16_t color,
    uint32_t alpha);

void fea_favourite_draw_question(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    int32_t size_q4,
    int32_t thickness_q4,
    uint16_t color,
    uint32_t alpha);

uint32_t fea_favourite_act_alpha(
    const fea_pose_t *pose,
    uint32_t maximum);

void fea_favourite_wisp_render(
    fea_favourite_profile_t profile,
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas);

void fea_favourite_karakuri_render(
    fea_favourite_profile_t profile,
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas);

void fea_favourite_sticker_render(
    fea_favourite_profile_t profile,
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas);
