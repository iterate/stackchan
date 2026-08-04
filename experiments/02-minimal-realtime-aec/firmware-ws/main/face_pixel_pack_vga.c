#include "face_pixel_pack_internal.h"

enum {
    VGA_BG_DARK = 0,
    VGA_BG_MID,
    VGA_BG_LIGHT,
    VGA_BLACK,
    VGA_SKIN_DARK,
    VGA_SKIN_SHADOW,
    VGA_SKIN_MID,
    VGA_SKIN_LIGHT,
    VGA_SKIN_HIGHLIGHT,
    VGA_BEARD_DARK,
    VGA_BEARD_MID,
    VGA_BEARD_LIGHT,
    VGA_BEARD_HIGHLIGHT,
    VGA_ROBE_DARK,
    VGA_ROBE_MID,
    VGA_ROBE_LIGHT,
    VGA_ROBE_HIGHLIGHT,
    VGA_GOLD,
    VGA_GOLD_HIGHLIGHT,
    VGA_EYE_WHITE,
    VGA_IRIS,
    VGA_LIP_DARK,
    VGA_LIP_MID,
    VGA_CAVITY,
    VGA_TEETH,
    VGA_TONGUE,
    VGA_COLOUR_COUNT,
};

static const uint16_t VGA_PALETTE[VGA_COLOUR_COUNT] = {
    FPP_RGB565(0x14, 0x10, 0x0c),
    FPP_RGB565(0x24, 0x1a, 0x12),
    FPP_RGB565(0x38, 0x28, 0x1c),
    FPP_RGB565(0x00, 0x00, 0x00),
    FPP_RGB565(0x4a, 0x30, 0x20),
    FPP_RGB565(0x6e, 0x4a, 0x32),
    FPP_RGB565(0x96, 0x68, 0x48),
    FPP_RGB565(0xc0, 0x8e, 0x62),
    FPP_RGB565(0xe0, 0xb4, 0x88),
    FPP_RGB565(0x58, 0x58, 0x50),
    FPP_RGB565(0x8c, 0x8c, 0x84),
    FPP_RGB565(0xc0, 0xc0, 0xb8),
    FPP_RGB565(0xe8, 0xe8, 0xe0),
    FPP_RGB565(0x28, 0x10, 0x38),
    FPP_RGB565(0x40, 0x20, 0x50),
    FPP_RGB565(0x58, 0x30, 0x70),
    FPP_RGB565(0x70, 0x48, 0xa0),
    FPP_RGB565(0xc0, 0x90, 0x30),
    FPP_RGB565(0xe8, 0xc0, 0x60),
    FPP_RGB565(0xe8, 0xe0, 0xd0),
    FPP_RGB565(0x78, 0x50, 0x28),
    FPP_RGB565(0x6e, 0x3a, 0x2e),
    FPP_RGB565(0x9a, 0x5a, 0x46),
    FPP_RGB565(0x20, 0x08, 0x08),
    FPP_RGB565(0xd8, 0xd0, 0xb8),
    FPP_RGB565(0xa0, 0x48, 0x38),
};

static void draw_vga_brow(
    fpp_surface_t *surface,
    int outer_x,
    int inner_x,
    int base_y,
    int outer_action,
    int inner_action)
{
    const int outer_y = base_y - outer_action / 12;
    const int inner_y = base_y - inner_action / 12;
    fpp_line(
        surface, outer_x, outer_y, inner_x, inner_y,
        VGA_BEARD_HIGHLIGHT);
    fpp_line(
        surface, outer_x, outer_y + 1, inner_x, inner_y + 1,
        VGA_BEARD_LIGHT);
    fpp_line(
        surface, outer_x + (outer_x < inner_x ? 1 : -1),
        outer_y + 2, inner_x, inner_y + 2, VGA_BEARD_MID);
}

void fpp_render_vga_elder(
    uint16_t *frame,
    const fpp_pose_t *pose,
    uint32_t sample_clock,
    face_pixel_pack_landmarks_t *landmarks)
{
    (void)sample_clock;
    fpp_surface_t art = fpp_surface_attach(frame, 160, 120);
    fpp_surface_t *surface = &art;
    fpp_clear(surface, VGA_BG_DARK);
    fpp_fill_ellipse(surface, 84, 55, 75, 61, VGA_BG_MID);
    fpp_fill_ellipse(surface, 86, 53, 53, 45, VGA_BG_LIGHT);
    fpp_fill_checker(
        surface, 0, 0, 27, 11,
        VGA_BG_DARK, VGA_BLACK, 1U);
    fpp_fill_checker(
        surface, 133, 0, 27, 11,
        VGA_BG_DARK, VGA_BLACK, 1U);

    const int body_x = pose->body_lean_x / 18;
    const int body_y = pose->body_lean_y / 32;
    const int centre_x =
        80 + body_x + pose->head_yaw / 26;
    const int face_y = fpp_clamp(
        49 + body_y + pose->head_pitch / 38 +
        pose->breath / 100 + pose->speech_bob / 92,
        46, 50);
    const int shoulder_y =
        97 + body_y - (pose->breath > 60 ? 1 : 0);

    fpp_fill_rect(
        surface, 8 + body_x, shoulder_y, 144,
        120 - shoulder_y, VGA_ROBE_MID);
    fpp_fill_ellipse(
        surface, centre_x, shoulder_y + 30,
        62, 30, VGA_ROBE_MID);
    fpp_fill_ellipse(
        surface, centre_x - 40, shoulder_y + 26,
        20, 18, VGA_ROBE_LIGHT);
    fpp_fill_ellipse(
        surface, centre_x + 44, shoulder_y + 30,
        20, 18, VGA_ROBE_DARK);
    fpp_hline(
        surface, 20 + body_x, 140 + body_x,
        shoulder_y + 6, VGA_GOLD);
    fpp_fill_circle(
        surface, centre_x, shoulder_y + 12, 3, VGA_GOLD);
    fpp_pixel(
        surface, centre_x - 1, shoulder_y + 11,
        VGA_GOLD_HIGHLIGHT);

    fpp_fill_ellipse(
        surface, centre_x - 27, face_y + 12,
        8, 22, VGA_BEARD_MID);
    fpp_fill_ellipse(
        surface, centre_x + 27, face_y + 12,
        8, 22, VGA_BEARD_DARK);
    fpp_fill_ellipse(
        surface, centre_x - 28, face_y + 8,
        4, 16, VGA_BEARD_LIGHT);

    fpp_fill_ellipse(
        surface, centre_x, face_y, 25, 30, VGA_SKIN_SHADOW);
    fpp_fill_ellipse(
        surface, centre_x - 3, face_y - 2,
        22, 27, VGA_SKIN_MID);
    fpp_fill_ellipse(
        surface, centre_x - 6, face_y - 4,
        17, 22, VGA_SKIN_LIGHT);
    fpp_fill_ellipse(
        surface, centre_x - 8, face_y - 7,
        10, 13, VGA_SKIN_HIGHLIGHT);
    fpp_fill_ellipse(
        surface, centre_x - 26, face_y + 4,
        4, 7, VGA_SKIN_MID);
    fpp_fill_ellipse(
        surface, centre_x + 26, face_y + 4,
        4, 7, VGA_SKIN_SHADOW);

    fpp_pixel(
        surface, centre_x + 6, face_y - 24, VGA_SKIN_SHADOW);
    fpp_pixel(
        surface, centre_x - 2, face_y - 26, VGA_SKIN_SHADOW);
    fpp_pixel(
        surface, centre_x + 12, face_y - 21, VGA_SKIN_SHADOW);
    for (int strand = 0; strand < 4; ++strand) {
        fpp_line(
            surface, centre_x - 20 + strand * 3,
            face_y - 27 - (strand & 1),
            centre_x - 26, face_y - 14 + strand,
            VGA_BEARD_LIGHT);
    }

    const int eye_y = face_y - 2;
    const int gaze_x = pose->gaze_x / 24;
    const int gaze_y = pose->gaze_y / 40;
    const int roll = pose->head_roll / 20;
    for (int side = 0; side < 2; ++side) {
        const int eye_x =
            centre_x + (side == 0 ? -11 : 11);
        const int side_roll = side == 0 ? roll : -roll;
        const int openness =
            side == 0 ? pose->eye_open_left : pose->eye_open_right;
        const int eye_height =
            fpp_clamp((openness * 5 + 127) / 255, 1, 5);
        fpp_fill_ellipse(
            surface, eye_x, eye_y + side_roll,
            8, 5, VGA_SKIN_SHADOW);
        fpp_fill_ellipse(
            surface, eye_x, eye_y + side_roll,
            6, fpp_max(eye_height - 1, 1), VGA_EYE_WHITE);
        if (eye_height > 1) {
            fpp_fill_circle(
                surface, eye_x + gaze_x,
                eye_y + gaze_y + side_roll,
                2, VGA_IRIS);
            fpp_pixel(
                surface, eye_x + gaze_x,
                eye_y + gaze_y + side_roll, VGA_BLACK);
            fpp_pixel(
                surface, eye_x + gaze_x - 1,
                eye_y + gaze_y - 1 + side_roll,
                VGA_BEARD_HIGHLIGHT);
        } else {
            fpp_hline(
                surface, eye_x - 5, eye_x + 5,
                eye_y + side_roll, VGA_BEARD_DARK);
        }
        fpp_hline(
            surface, eye_x - 5, eye_x + 5,
            eye_y + 4 + side_roll, VGA_SKIN_SHADOW);
    }
    draw_vga_brow(
        surface, centre_x - 19, centre_x - 3,
        eye_y - 8 + roll,
        pose->brow_outer_left, pose->brow_inner);
    draw_vga_brow(
        surface, centre_x + 19, centre_x + 3,
        eye_y - 8 - roll,
        pose->brow_outer_right, pose->brow_inner);

    fpp_vline(
        surface, centre_x - 1, face_y + 2,
        face_y + 12, VGA_SKIN_SHADOW);
    fpp_vline(
        surface, centre_x - 2, face_y + 6,
        face_y + 12, VGA_SKIN_MID);
    fpp_fill_ellipse(
        surface, centre_x - 1, face_y + 13,
        4, 3, VGA_SKIN_MID);
    fpp_pixel(
        surface, centre_x - 3, face_y + 12,
        VGA_SKIN_HIGHLIGHT);
    fpp_pixel(
        surface, centre_x - 4, face_y + 14, VGA_SKIN_DARK);
    fpp_pixel(
        surface, centre_x + 2, face_y + 14, VGA_SKIN_DARK);

    const int beard_y = face_y + 39;
    fpp_fill_ellipse(
        surface, centre_x, beard_y, 29, 27, VGA_BEARD_MID);
    fpp_fill_ellipse(
        surface, centre_x - 2, beard_y - 3,
        25, 23, VGA_BEARD_LIGHT);
    fpp_fill_ellipse(
        surface, centre_x, beard_y + 8,
        18, 16, VGA_BEARD_MID);
    for (int offset = -24; offset <= 24; offset += 3) {
        const uint32_t hash =
            fpp_hash32(0xbea4d0U ^ (uint32_t)(offset + 40));
        const int strand_x = centre_x + offset;
        const uint32_t remaining =
            (uint32_t)(29 * 29 - offset * offset);
        const int edge = (int)fpp_isqrt(
            remaining * (27U * 27U) / (29U * 29U));
        const int start_y =
            beard_y + 2 + (int)((hash >> 8U) % 5U);
        const int end_y = fpp_min(
            start_y + 6 + (int)(hash % 9U),
            beard_y + edge - 2);
        if (end_y > start_y) {
            fpp_vline(
                surface, strand_x, start_y, end_y,
                (hash & 2U) != 0U
                    ? VGA_BEARD_DARK
                    : VGA_BEARD_HIGHLIGHT);
        }
    }

    const int mouth_y = face_y + 22;
    fpp_fill_ellipse(
        surface, centre_x, mouth_y + 1,
        16, 8, VGA_BEARD_DARK);
    /*
     * The moustache sits behind the lips. Drawing it first preserves the
     * upper-lip highlight and cavity silhouette at wide AA/OH openings.
     */
    fpp_fill_ellipse(
        surface, centre_x - 9, mouth_y - 3,
        8, 2, VGA_BEARD_LIGHT);
    fpp_fill_ellipse(
        surface, centre_x + 9, mouth_y - 3,
        8, 2, VGA_BEARD_LIGHT);
    const fpp_lips_t lips = {
        centre_x, mouth_y, 12, 16,
        VGA_LIP_DARK, VGA_LIP_MID, VGA_CAVITY,
        VGA_TEETH, VGA_TONGUE,
    };
    fpp_draw_polygon_lips(surface, &lips, pose);
    fpp_hline(
        surface, centre_x - 6, centre_x + 6,
        mouth_y - 2 - pose->mouth_open / 128,
        VGA_LIP_MID);

    if (pose->cheek > 28) {
        const uint8_t colour =
            pose->cheek > 86 ? VGA_LIP_MID : VGA_SKIN_SHADOW;
        fpp_fill_checker(
            surface, centre_x - 25, face_y + 7,
            12, 7, VGA_SKIN_MID, colour, 2U);
        fpp_fill_checker(
            surface, centre_x + 14, face_y + 7,
            12, 7, VGA_SKIN_SHADOW, colour, 2U);
    }

    if (landmarks != NULL) {
        landmarks->face = (face_pixel_pack_bounds_t){
            (uint8_t)(centre_x - 34),
            (uint8_t)fpp_max(face_y - 30, 5),
            68, 97,
        };
        landmarks->left_eye = (face_pixel_pack_bounds_t){
            (uint8_t)(centre_x - 21),
            (uint8_t)(eye_y - 12 + roll),
            20, 20,
        };
        landmarks->right_eye = (face_pixel_pack_bounds_t){
            (uint8_t)(centre_x + 1),
            (uint8_t)(eye_y - 12 - roll),
            20, 20,
        };
        landmarks->mouth = (face_pixel_pack_bounds_t){
            (uint8_t)(centre_x - 18),
            (uint8_t)(mouth_y - 11),
            36, 28,
        };
    }
    fpp_present(frame, surface, 1, 1, VGA_PALETTE);
}
