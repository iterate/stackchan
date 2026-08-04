#include "face_pixel_pack_internal.h"

enum {
    TALKIE_BG = 0,
    TALKIE_SPOT,
    TALKIE_OUTLINE,
    TALKIE_SKIN,
    TALKIE_SKIN_SHADOW,
    TALKIE_SKIN_HIGHLIGHT,
    TALKIE_BAND,
    TALKIE_BAND_DARK,
    TALKIE_BAND_DOT,
    TALKIE_SHIRT,
    TALKIE_SHIRT_SHADOW,
    TALKIE_GOLD,
    TALKIE_EYE_WHITE,
    TALKIE_PUPIL,
    TALKIE_LIP_DARK,
    TALKIE_LIP_MID,
    TALKIE_CAVITY,
    TALKIE_TEETH,
    TALKIE_TONGUE,
    TALKIE_HAIR,
    TALKIE_BAR_BG,
    TALKIE_BAR_TEXT,
    TALKIE_BAR_HIGHLIGHT,
    TALKIE_COLOUR_COUNT,
};

static const uint16_t TALKIE_PALETTE[TALKIE_COLOUR_COUNT] = {
    FPP_RGB565(0x18, 0x28, 0x30),
    FPP_RGB565(0x2a, 0x40, 0x40),
    FPP_RGB565(0x00, 0x00, 0x00),
    FPP_RGB565(0xe0, 0xa0, 0x70),
    FPP_RGB565(0xb8, 0x78, 0x50),
    FPP_RGB565(0xf8, 0xc8, 0x90),
    FPP_RGB565(0xc0, 0x30, 0x28),
    FPP_RGB565(0x88, 0x20, 0x18),
    FPP_RGB565(0xf0, 0xe8, 0xd0),
    FPP_RGB565(0xe8, 0xe0, 0xd0),
    FPP_RGB565(0xb0, 0xa8, 0x98),
    FPP_RGB565(0xe8, 0xb8, 0x30),
    FPP_RGB565(0xff, 0xff, 0xff),
    FPP_RGB565(0x20, 0x18, 0x10),
    FPP_RGB565(0x70, 0x28, 0x20),
    FPP_RGB565(0xa0, 0x40, 0x30),
    FPP_RGB565(0x30, 0x0c, 0x08),
    FPP_RGB565(0xf8, 0xf0, 0xe0),
    FPP_RGB565(0xc0, 0x50, 0x40),
    FPP_RGB565(0x40, 0x28, 0x18),
    FPP_RGB565(0x28, 0x24, 0x30),
    FPP_RGB565(0x98, 0x90, 0xb8),
    FPP_RGB565(0xe0, 0xd8, 0xf8),
};

static void draw_talkie_brow(
    fpp_surface_t *surface,
    int outer_x,
    int inner_x,
    int base_y,
    int outer_action,
    int inner_action)
{
    const int outer_y = base_y - outer_action / 18;
    const int inner_y = base_y - inner_action / 18;
    fpp_line(
        surface, outer_x, outer_y, inner_x, inner_y,
        TALKIE_OUTLINE);
    fpp_line(
        surface, outer_x, outer_y + 1, inner_x, inner_y + 1,
        TALKIE_OUTLINE);
}

void fpp_render_talkie_closeup(
    uint16_t *frame,
    const fpp_pose_t *pose,
    uint32_t sample_clock,
    face_pixel_pack_landmarks_t *landmarks)
{
    (void)sample_clock;
    fpp_surface_t art = fpp_surface_attach(frame, 80, 60);
    fpp_surface_t *surface = &art;
    fpp_clear(surface, TALKIE_BG);
    fpp_fill_ellipse(surface, 40, 26, 30, 24, TALKIE_SPOT);

    const int body_x = pose->body_lean_x / 45;
    const int body_y = pose->body_lean_y / 64;
    const int head_x =
        40 + body_x + pose->head_yaw / 46;
    const int face_y =
        26 + body_y + pose->head_pitch / 54 +
        pose->breath / 128;
    const int roll = pose->head_roll / 30;

    fpp_fill_rect(
        surface, 12 + body_x, 47 + body_y,
        56, 6, TALKIE_SHIRT);
    fpp_fill_rect(
        surface, 12 + body_x, 47 + body_y,
        12, 6, TALKIE_SHIRT_SHADOW);
    fpp_hline(
        surface, 12 + body_x, 67 + body_x,
        47 + body_y, TALKIE_OUTLINE);
    fpp_line(
        surface, 36 + body_x, 53 + body_y,
        40 + body_x, 48 + body_y, TALKIE_SHIRT_SHADOW);
    fpp_line(
        surface, 44 + body_x, 53 + body_y,
        40 + body_x, 48 + body_y, TALKIE_SHIRT_SHADOW);

    fpp_fill_rect(
        surface, head_x - 5, face_y + 16,
        10, 7, TALKIE_SKIN);
    fpp_vline(
        surface, head_x - 5, face_y + 16,
        face_y + 22, TALKIE_OUTLINE);
    fpp_vline(
        surface, head_x + 4, face_y + 16,
        face_y + 22, TALKIE_OUTLINE);
    fpp_fill_ellipse(
        surface, head_x - 17, face_y + 3,
        3, 5, TALKIE_OUTLINE);
    fpp_fill_ellipse(
        surface, head_x + 17, face_y + 3,
        3, 5, TALKIE_OUTLINE);
    fpp_fill_ellipse(
        surface, head_x - 17, face_y + 3,
        2, 4, TALKIE_SKIN);
    fpp_fill_ellipse(
        surface, head_x + 17, face_y + 3,
        2, 4, TALKIE_SKIN_SHADOW);

    fpp_fill_ellipse(
        surface, head_x, face_y + 12,
        12, 9, TALKIE_OUTLINE);
    fpp_fill_ellipse(
        surface, head_x, face_y + 12,
        11, 8, TALKIE_SKIN);
    fpp_fill_ellipse(
        surface, head_x, face_y, 17, 15, TALKIE_OUTLINE);
    fpp_fill_ellipse(
        surface, head_x, face_y, 16, 14, TALKIE_SKIN);
    fpp_fill_ellipse(
        surface, head_x + 9, face_y + 4,
        6, 9, TALKIE_SKIN_SHADOW);
    fpp_fill_ellipse(
        surface, head_x - 4, face_y - 2,
        10, 10, TALKIE_SKIN);
    fpp_fill_ellipse(
        surface, head_x - 7, face_y - 4,
        6, 6, TALKIE_SKIN_HIGHLIGHT);
    fpp_fill_ellipse_checker(
        surface, head_x, face_y + 14,
        8, 4, TALKIE_SKIN, TALKIE_SKIN_SHADOW, 1U);

    fpp_pixel(
        surface, head_x - 18, face_y + 8,
        TALKIE_GOLD);
    fpp_fill_circle(
        surface, head_x - 18 + pose->speech_bob / 70,
        face_y + 10, 1, TALKIE_GOLD);

    fpp_fill_ellipse(
        surface, head_x, face_y - 11,
        17, 8, TALKIE_OUTLINE);
    fpp_fill_ellipse(
        surface, head_x, face_y - 11,
        16, 7, TALKIE_BAND);
    fpp_fill_rect(
        surface, head_x - 16, face_y - 10,
        33, 4, TALKIE_BAND);
    fpp_hline(
        surface, head_x - 16, head_x + 16,
        face_y - 6, TALKIE_OUTLINE);
    fpp_fill_ellipse(
        surface, head_x - 12, face_y - 14,
        6, 3, TALKIE_BAND_DARK);
    for (int dot = 0; dot < 7; ++dot) {
        const uint32_t hash =
            fpp_hash32(0xd07U ^ (uint32_t)dot);
        const int offset_x = (int)(hash % 27U) - 13;
        const int offset_y =
            -8 - (int)((hash >> 8U) % 6U);
        fpp_pixel(
            surface, head_x + offset_x,
            face_y + offset_y, TALKIE_BAND_DOT);
    }
    fpp_line(
        surface, head_x + 15, face_y - 10,
        head_x + 20, face_y - 5, TALKIE_BAND);
    fpp_line(
        surface, head_x + 16, face_y - 11,
        head_x + 21, face_y - 7, TALKIE_BAND_DARK);

    fpp_vline(
        surface, head_x - 15, face_y + 2,
        face_y + 6, TALKIE_HAIR);
    fpp_vline(
        surface, head_x + 15, face_y + 2,
        face_y + 5, TALKIE_HAIR);

    const int gaze_x = pose->gaze_x / 24;
    const int gaze_y = pose->gaze_y / 40;
    const int eye_y = face_y - 1;
    for (int side = 0; side < 2; ++side) {
        const int eye_x =
            head_x + (side == 0 ? -7 : 7);
        const int side_roll = side == 0 ? roll : -roll;
        const int openness =
            side == 0 ? pose->eye_open_left : pose->eye_open_right;
        const int eye_height =
            fpp_clamp((openness * 4 + 127) / 255, 1, 4);
        fpp_fill_ellipse(
            surface, eye_x, eye_y + side_roll,
            5, 4, TALKIE_OUTLINE);
        fpp_fill_ellipse(
            surface, eye_x, eye_y + side_roll,
            4, eye_height, TALKIE_EYE_WHITE);
        if (eye_height > 1) {
            fpp_fill_rect(
                surface, eye_x - 1 + gaze_x,
                eye_y - 1 + gaze_y + side_roll,
                2, 2, TALKIE_PUPIL);
            fpp_pixel(
                surface, eye_x + gaze_x,
                eye_y - 1 + gaze_y + side_roll,
                TALKIE_EYE_WHITE);
        } else {
            fpp_hline(
                surface, eye_x - 4, eye_x + 4,
                eye_y + side_roll, TALKIE_OUTLINE);
        }
    }
    draw_talkie_brow(
        surface, head_x - 12, head_x - 3,
        eye_y - 7 + roll,
        pose->brow_outer_left, pose->brow_inner);
    draw_talkie_brow(
        surface, head_x + 12, head_x + 3,
        eye_y - 7 - roll,
        pose->brow_outer_right, pose->brow_inner);

    fpp_line(
        surface, head_x + 10, face_y + 4,
        head_x + 13, face_y + 9, TALKIE_SKIN_SHADOW);
    fpp_pixel(
        surface, head_x + 11, face_y + 6,
        TALKIE_LIP_DARK);
    fpp_line(
        surface, head_x - 1, face_y + 2,
        head_x - 2, face_y + 7, TALKIE_SKIN_SHADOW);
    fpp_fill_ellipse(
        surface, head_x - 1, face_y + 8,
        3, 2, TALKIE_SKIN_SHADOW);
    fpp_pixel(
        surface, head_x - 2, face_y + 7,
        TALKIE_SKIN_HIGHLIGHT);

    const int mouth_y = face_y + 12;
    const fpp_lips_t lips = {
        head_x, mouth_y, 8, 8,
        TALKIE_LIP_DARK, TALKIE_LIP_MID, TALKIE_CAVITY,
        TALKIE_TEETH, TALKIE_TONGUE,
    };
    fpp_draw_polygon_lips(surface, &lips, pose);
    if (pose->cheek > 46) {
        const uint8_t cheek =
            pose->cheek > 96 ? TALKIE_LIP_MID : TALKIE_SKIN_SHADOW;
        fpp_fill_checker(
            surface, head_x - 14, face_y + 5,
            5, 3, TALKIE_SKIN, cheek, 2U);
        fpp_fill_checker(
            surface, head_x + 10, face_y + 5,
            5, 3, TALKIE_SKIN_SHADOW, cheek, 2U);
    }

    fpp_fill_rect(surface, 0, 53, 80, 7, TALKIE_BAR_BG);
    fpp_hline(surface, 0, 79, 53, TALKIE_OUTLINE);
    fpp_text3x5(surface, 3, 54, "LOOK", TALKIE_BAR_TEXT);
    fpp_text3x5(surface, 23, 54, "TALK", TALKIE_BAR_HIGHLIGHT);
    fpp_text3x5(surface, 43, 54, "USE", TALKIE_BAR_TEXT);
    fpp_text3x5(surface, 59, 54, "WALK", TALKIE_BAR_TEXT);

    if (landmarks != NULL) {
        landmarks->face = (face_pixel_pack_bounds_t){
            (uint8_t)((head_x - 22) * 2),
            (uint8_t)fpp_max((face_y - 20) * 2, 4),
            88, 96,
        };
        landmarks->left_eye = (face_pixel_pack_bounds_t){
            (uint8_t)((head_x - 13) * 2),
            (uint8_t)((eye_y - 10 + roll) * 2),
            24, 24,
        };
        landmarks->right_eye = (face_pixel_pack_bounds_t){
            (uint8_t)((head_x + 1) * 2),
            (uint8_t)((eye_y - 10 - roll) * 2),
            24, 24,
        };
        landmarks->mouth = (face_pixel_pack_bounds_t){
            (uint8_t)((head_x - 10) * 2),
            (uint8_t)((mouth_y - 5) * 2),
            40, 24,
        };
    }
    fpp_present(frame, surface, 2, 2, TALKIE_PALETTE);
}
