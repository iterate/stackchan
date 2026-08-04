#include "face_pixel_pack_internal.h"

enum {
    EGA_BLACK = 0,
    EGA_BLUE,
    EGA_GREEN,
    EGA_CYAN,
    EGA_RED,
    EGA_MAGENTA,
    EGA_BROWN,
    EGA_LIGHT_GRAY,
    EGA_DARK_GRAY,
    EGA_LIGHT_BLUE,
    EGA_LIGHT_GREEN,
    EGA_LIGHT_CYAN,
    EGA_LIGHT_RED,
    EGA_LIGHT_MAGENTA,
    EGA_YELLOW,
    EGA_WHITE,
};

static const uint16_t EGA_PALETTE[16] = {
    FPP_RGB565(0x00, 0x00, 0x00),
    FPP_RGB565(0x00, 0x00, 0xaa),
    FPP_RGB565(0x00, 0xaa, 0x00),
    FPP_RGB565(0x00, 0xaa, 0xaa),
    FPP_RGB565(0xaa, 0x00, 0x00),
    FPP_RGB565(0xaa, 0x00, 0xaa),
    FPP_RGB565(0xaa, 0x55, 0x00),
    FPP_RGB565(0xaa, 0xaa, 0xaa),
    FPP_RGB565(0x55, 0x55, 0x55),
    FPP_RGB565(0x55, 0x55, 0xff),
    FPP_RGB565(0x55, 0xff, 0x55),
    FPP_RGB565(0x55, 0xff, 0xff),
    FPP_RGB565(0xff, 0x55, 0x55),
    FPP_RGB565(0xff, 0x55, 0xff),
    FPP_RGB565(0xff, 0xff, 0x55),
    FPP_RGB565(0xff, 0xff, 0xff),
};

static void draw_brow(
    fpp_surface_t *surface,
    int outer_x,
    int inner_x,
    int base_y,
    int outer_action,
    int inner_action)
{
    const int outer_y = base_y - outer_action / 14;
    const int inner_y = base_y - inner_action / 14;
    fpp_line(
        surface, outer_x, outer_y, inner_x, inner_y, EGA_BROWN);
    fpp_line(
        surface, outer_x, outer_y + 1,
        inner_x, inner_y + 1, EGA_BROWN);
    fpp_line(
        surface, outer_x + (outer_x < inner_x ? 1 : -1),
        outer_y + 2, inner_x, inner_y + 2, EGA_RED);
}

void fpp_render_ega_quest(
    uint16_t *frame,
    const fpp_pose_t *pose,
    uint32_t sample_clock,
    face_pixel_pack_landmarks_t *landmarks)
{
    (void)sample_clock;
    fpp_surface_t art = fpp_surface_attach(frame, 80, 60);
    fpp_surface_t *surface = &art;
    fpp_clear(surface, EGA_BLUE);
    fpp_rect(surface, 0, 0, 80, 60, EGA_WHITE);
    fpp_rect(surface, 1, 1, 78, 58, EGA_DARK_GRAY);

    const int body_x = pose->body_lean_x / 42;
    const int body_y = pose->body_lean_y / 64;
    const int head_x =
        40 + body_x + pose->head_yaw / 48;
    const int head_y =
        27 + body_y + pose->head_pitch / 52 +
        pose->breath / 128;
    const int left_roll = pose->head_roll / 32;
    const int right_roll = -left_roll;

    fpp_fill_rect(
        surface, 18 + body_x, 49 + body_y, 44, 9, EGA_GREEN);
    fpp_fill_checker(
        surface, 18 + body_x, 49 + body_y, 44, 2,
        EGA_GREEN, EGA_LIGHT_GREEN, 2U);
    fpp_fill_rect(
        surface, head_x - 4, head_y + 17, 8, 7, EGA_LIGHT_RED);
    fpp_fill_checker(
        surface, head_x - 4, head_y + 17, 8, 2,
        EGA_LIGHT_RED, EGA_RED, 2U);

    fpp_fill_ellipse(
        surface, head_x, head_y, 13, 15, EGA_LIGHT_RED);
    fpp_fill_ellipse_checker(
        surface, head_x + 6, head_y + 2, 7, 12,
        EGA_LIGHT_RED, EGA_RED, 2U);
    fpp_fill_ellipse(
        surface, head_x - 3, head_y - 1, 8, 11,
        EGA_LIGHT_RED);
    fpp_fill_rect(
        surface, head_x - 15, head_y + 1, 3, 5,
        EGA_LIGHT_RED);
    fpp_fill_rect(
        surface, head_x + 12, head_y + 1, 3, 5,
        EGA_LIGHT_RED);
    fpp_pixel(surface, head_x - 14, head_y + 3, EGA_RED);
    fpp_pixel(surface, head_x + 13, head_y + 3, EGA_RED);

    const int cap_y = head_y - 14;
    fpp_fill_checker(
        surface, head_x - 12, head_y - 12, 24, 4,
        EGA_BROWN, EGA_RED, 2U);
    fpp_fill_ellipse(
        surface, head_x, cap_y, 14, 5, EGA_GREEN);
    fpp_fill_rect(
        surface, head_x - 14, cap_y + 1, 28, 3, EGA_GREEN);
    fpp_fill_checker(
        surface, head_x - 14, cap_y - 2, 28, 3,
        EGA_GREEN, EGA_LIGHT_GREEN, 2U);
    fpp_hline(
        surface, head_x - 14, head_x + 14, cap_y + 4,
        EGA_BLACK);
    const int feather_sway =
        pose->head_roll / 42 + pose->breath / 80;
    fpp_line(
        surface, head_x + 12, cap_y - 1,
        head_x + 17 + feather_sway, cap_y - 9, EGA_YELLOW);
    fpp_line(
        surface, head_x + 13, cap_y,
        head_x + 19 + feather_sway, cap_y - 7, EGA_YELLOW);
    fpp_pixel(
        surface, head_x + 17 + feather_sway,
        cap_y - 10, EGA_WHITE);

    const int gaze_x = pose->gaze_x / 24;
    const int gaze_y = pose->gaze_y / 40;
    const int eye_y = head_y - 2;
    for (int side = 0; side < 2; ++side) {
        const int eye_x = head_x + (side == 0 ? -6 : 6);
        const int roll = side == 0 ? left_roll : right_roll;
        const int openness =
            side == 0 ? pose->eye_open_left : pose->eye_open_right;
        const int visible_height =
            fpp_clamp((openness * 5 + 127) / 255, 1, 5);
        fpp_fill_rect(
            surface, eye_x - 3, eye_y - 2 + roll,
            7, 5, EGA_DARK_GRAY);
        fpp_fill_rect(
            surface, eye_x - 2, eye_y + 2 - visible_height + roll,
            5, visible_height, EGA_WHITE);
        if (visible_height >= 2) {
            fpp_fill_rect(
                surface,
                eye_x - 1 + gaze_x,
                eye_y - 1 + gaze_y + roll,
                2, fpp_min(3, visible_height), EGA_LIGHT_BLUE);
            fpp_pixel(
                surface, eye_x - 1 + gaze_x,
                eye_y + gaze_y + roll, EGA_BLACK);
        } else {
            fpp_hline(
                surface, eye_x - 3, eye_x + 3,
                eye_y + roll, EGA_BROWN);
        }
    }

    draw_brow(
        surface, head_x - 10, head_x - 2, eye_y - 5 + left_roll,
        pose->brow_outer_left, pose->brow_inner);
    draw_brow(
        surface, head_x + 10, head_x + 2, eye_y - 5 + right_roll,
        pose->brow_outer_right, pose->brow_inner);

    fpp_vline(
        surface, head_x, head_y + 2, head_y + 5, EGA_RED);
    fpp_hline(
        surface, head_x, head_x + 1, head_y + 5, EGA_RED);

    static const uint8_t MOUTH_COLOURS[5] = {
        EGA_RED, EGA_LIGHT_RED, EGA_BLACK, EGA_WHITE,
        EGA_LIGHT_MAGENTA,
    };
    const int mouth_y = head_y + 10;
    fpp_draw_mouth_sprite(
        surface, head_x, mouth_y,
        fpp_classify_mouth(pose), MOUTH_COLOURS);
    const int left_corner_y =
        mouth_y - pose->mouth_corner_left / 18;
    const int right_corner_y =
        mouth_y - pose->mouth_corner_right / 18;
    fpp_line(
        surface, head_x - 7, mouth_y,
        head_x - 9, left_corner_y, EGA_RED);
    fpp_line(
        surface, head_x + 7, mouth_y,
        head_x + 9, right_corner_y, EGA_RED);

    if (pose->cheek > 24) {
        const uint8_t cheek_colour =
            pose->cheek > 70 ? EGA_LIGHT_MAGENTA : EGA_RED;
        fpp_fill_checker(
            surface, head_x - 13, head_y + 4, 5, 4,
            EGA_LIGHT_RED, cheek_colour, 3U);
        fpp_fill_checker(
            surface, head_x + 9, head_y + 4, 5, 4,
            EGA_LIGHT_RED, cheek_colour, 3U);
    }
    fpp_fill_checker(
        surface, head_x - 4, head_y + 13, 9, 2,
        EGA_LIGHT_RED, EGA_RED, 1U);

    fpp_fill_rect(surface, 16, 51, 48, 8, EGA_BLACK);
    fpp_rect(surface, 16, 51, 48, 8, EGA_LIGHT_GRAY);
    fpp_hline(surface, 17, 62, 58, EGA_DARK_GRAY);
    fpp_text3x5(surface, 23, 53, "SIR ROWAN", EGA_WHITE);

    if (landmarks != NULL) {
        landmarks->face = (face_pixel_pack_bounds_t){
            (uint8_t)((head_x - 20) * 2),
            (uint8_t)fpp_max((cap_y - 11) * 2, 4),
            80, 96,
        };
        landmarks->left_eye = (face_pixel_pack_bounds_t){
            (uint8_t)((head_x - 10) * 2),
            (uint8_t)((eye_y - 8 + left_roll) * 2),
            16, 20,
        };
        landmarks->right_eye = (face_pixel_pack_bounds_t){
            (uint8_t)((head_x + 2) * 2),
            (uint8_t)((eye_y - 8 + right_roll) * 2),
            16, 20,
        };
        landmarks->mouth = (face_pixel_pack_bounds_t){
            (uint8_t)((head_x - 10) * 2),
            (uint8_t)((mouth_y - 5) * 2),
            40, 22,
        };
    }
    fpp_present(frame, surface, 2, 2, EGA_PALETTE);
}
