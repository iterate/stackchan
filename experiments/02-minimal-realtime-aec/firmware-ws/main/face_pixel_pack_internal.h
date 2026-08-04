#pragma once

#include <stddef.h>
#include <stdint.h>

#include "face_pixel_pack.h"
#include "face_stage.h"

#define FPP_RGB565(red, green, blue)                                      \
    ((uint16_t)((((uint16_t)(red) >> 3U) << 11U) |                       \
                (((uint16_t)(green) >> 2U) << 5U) |                     \
                ((uint16_t)(blue) >> 3U)))

typedef struct {
    uint8_t *pixels;
    int width;
    int height;
} fpp_surface_t;

typedef struct {
    int mouth_open;
    int mouth_width;
    int mouth_round;
    int mouth_press;
    int mouth_teeth;
    int mouth_corner_left;
    int mouth_corner_right;
    int eye_open_left;
    int eye_open_right;
    int gaze_x;
    int gaze_y;
    int brow_inner;
    int brow_outer_left;
    int brow_outer_right;
    int cheek;
    int head_roll;
    int head_yaw;
    int head_pitch;
    int body_lean_x;
    int body_lean_y;
    int valence;
    int arousal;
    int breath;
    int speech_bob;
    int stage_expression;
} fpp_pose_t;

typedef enum {
    FPP_MOUTH_REST = 0,
    FPP_MOUTH_MBP,
    FPP_MOUTH_FV,
    FPP_MOUTH_SS,
    FPP_MOUTH_EE,
    FPP_MOUTH_EH,
    FPP_MOUTH_AA,
    FPP_MOUTH_OO,
    FPP_MOUTH_OH,
    FPP_MOUTH_SMALL,
    FPP_MOUTH_SHAPE_COUNT,
} fpp_mouth_shape_t;

typedef struct {
    int centre_x;
    int centre_y;
    int half_width;
    int max_open;
    uint8_t lip_dark;
    uint8_t lip_mid;
    uint8_t cavity;
    uint8_t teeth;
    uint8_t tongue;
} fpp_lips_t;

typedef void (*fpp_renderer_t)(
    uint16_t *frame,
    const fpp_pose_t *pose,
    uint32_t sample_clock,
    face_pixel_pack_landmarks_t *landmarks);

int fpp_clamp(int value, int low, int high);
int fpp_abs(int value);
int fpp_min(int first, int second);
int fpp_max(int first, int second);
int fpp_lerp(int first, int second, int weight);
uint32_t fpp_hash32(uint32_t value);
int32_t fpp_isqrt(uint32_t value);

fpp_surface_t fpp_surface_attach(
    uint16_t *frame, int width, int height);
void fpp_present(
    uint16_t *frame,
    const fpp_surface_t *surface,
    int scale_x,
    int scale_y,
    const uint16_t *palette);
void fpp_clear(fpp_surface_t *surface, uint8_t colour);
void fpp_pixel(
    fpp_surface_t *surface, int x, int y, uint8_t colour);
void fpp_hline(
    fpp_surface_t *surface, int x0, int x1, int y, uint8_t colour);
void fpp_vline(
    fpp_surface_t *surface, int x, int y0, int y1, uint8_t colour);
void fpp_fill_rect(
    fpp_surface_t *surface,
    int x,
    int y,
    int width,
    int height,
    uint8_t colour);
void fpp_rect(
    fpp_surface_t *surface,
    int x,
    int y,
    int width,
    int height,
    uint8_t colour);
void fpp_line(
    fpp_surface_t *surface,
    int x0,
    int y0,
    int x1,
    int y1,
    uint8_t colour);
void fpp_fill_ellipse(
    fpp_surface_t *surface,
    int centre_x,
    int centre_y,
    int radius_x,
    int radius_y,
    uint8_t colour);
void fpp_fill_circle(
    fpp_surface_t *surface,
    int centre_x,
    int centre_y,
    int radius,
    uint8_t colour);
void fpp_fill_checker(
    fpp_surface_t *surface,
    int x,
    int y,
    int width,
    int height,
    uint8_t first,
    uint8_t second,
    unsigned density);
void fpp_fill_ellipse_checker(
    fpp_surface_t *surface,
    int centre_x,
    int centre_y,
    int radius_x,
    int radius_y,
    uint8_t first,
    uint8_t second,
    unsigned density);
void fpp_dither_bayer_1bit(fpp_surface_t *surface);
void fpp_text3x5(
    fpp_surface_t *surface,
    int x,
    int y,
    const char *text,
    uint8_t colour);

void fpp_resolve_pose(
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint32_t salt,
    fpp_pose_t *pose);
fpp_mouth_shape_t fpp_classify_mouth(const fpp_pose_t *pose);
void fpp_draw_mouth_sprite(
    fpp_surface_t *surface,
    int centre_x,
    int centre_y,
    fpp_mouth_shape_t shape,
    const uint8_t colours[5]);
void fpp_draw_polygon_lips(
    fpp_surface_t *surface,
    const fpp_lips_t *lips,
    const fpp_pose_t *pose);

void fpp_render_ega_quest(
    uint16_t *frame,
    const fpp_pose_t *pose,
    uint32_t sample_clock,
    face_pixel_pack_landmarks_t *landmarks);
void fpp_render_vga_elder(
    uint16_t *frame,
    const fpp_pose_t *pose,
    uint32_t sample_clock,
    face_pixel_pack_landmarks_t *landmarks);
void fpp_render_talkie_closeup(
    uint16_t *frame,
    const fpp_pose_t *pose,
    uint32_t sample_clock,
    face_pixel_pack_landmarks_t *landmarks);
void fpp_render_dithered_rogue(
    uint16_t *frame,
    const fpp_pose_t *pose,
    uint32_t sample_clock,
    face_pixel_pack_landmarks_t *landmarks);
