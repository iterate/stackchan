#include "face_pixel_pack_internal.h"

static const uint16_t ROGUE_PALETTE[2] = {
    FPP_RGB565(0x0e, 0x0e, 0x12),
    FPP_RGB565(0xe8, 0xe8, 0xe0),
};

static void add_light(
    fpp_surface_t *surface,
    int centre_x,
    int centre_y,
    int radius_x,
    int radius_y,
    int amount)
{
    for (int offset_y = -radius_y;
         offset_y <= radius_y;
         ++offset_y) {
        const int y = centre_y + offset_y;
        if (y < 0 || y >= surface->height) {
            continue;
        }
        const uint32_t remaining =
            (uint32_t)(radius_y * radius_y -
                       offset_y * offset_y);
        const int offset_x = (int)fpp_isqrt(
            remaining * (uint32_t)(radius_x * radius_x) /
            (uint32_t)(radius_y * radius_y));
        for (int x = fpp_max(centre_x - offset_x, 0);
             x <= fpp_min(centre_x + offset_x,
                          surface->width - 1);
             ++x) {
            const int falloff =
                256 -
                fpp_abs(x - centre_x) * 256 / (radius_x + 1);
            uint8_t *pixel =
                &surface->pixels[
                    (size_t)y * (size_t)surface->width +
                    (size_t)x];
            *pixel = (uint8_t)fpp_clamp(
                *pixel + amount * falloff / 256, 0, 255);
        }
    }
}

static void draw_rogue_brow(
    fpp_surface_t *surface,
    int outer_x,
    int inner_x,
    int base_y,
    int outer_action,
    int inner_action)
{
    const int outer_y = base_y - outer_action / 16;
    const int inner_y = base_y - inner_action / 16;
    fpp_line(
        surface, outer_x, outer_y, inner_x, inner_y, 205);
    fpp_line(
        surface, outer_x, outer_y + 1, inner_x, inner_y + 1, 30);
}

void fpp_render_dithered_rogue(
    uint16_t *frame,
    const fpp_pose_t *pose,
    uint32_t sample_clock,
    face_pixel_pack_landmarks_t *landmarks)
{
    (void)sample_clock;
    fpp_surface_t art = fpp_surface_attach(frame, 160, 120);
    fpp_surface_t *surface = &art;
    fpp_clear(surface, 2U);
    for (int star = 0; star < 22; ++star) {
        const uint32_t hash =
            fpp_hash32(0x57a45U ^ (uint32_t)star);
        const int x = (int)(hash % 160U);
        const int y = (int)((hash >> 8U) % 43U);
        fpp_pixel(surface, x, y, 210);
        if ((hash & 3U) == 0U) {
            fpp_pixel(surface, x - 1, y, 110);
            fpp_pixel(surface, x + 1, y, 110);
            fpp_pixel(surface, x, y - 1, 110);
            fpp_pixel(surface, x, y + 1, 110);
        }
    }

    const int body_x = pose->body_lean_x / 12;
    const int body_y = pose->body_lean_y / 28;
    const int centre_x = fpp_clamp(
        80 + body_x + pose->head_yaw / 22 +
        pose->head_roll / 8,
        62, 98);
    const int top = fpp_clamp(
        12 + body_y + pose->head_pitch / 34 +
        pose->breath / 100 + pose->speech_bob / 96,
        8, 12);
    const int face_y = top + 52;
    const int roll = pose->head_roll / 18;

    fpp_fill_ellipse(
        surface, centre_x, 128 + body_y, 66, 40, 52);
    fpp_fill_ellipse(
        surface, centre_x - 30, 126 + body_y, 24, 30, 64);
    for (int fold = 0; fold < 5; ++fold) {
        fpp_line(
            surface, centre_x - 40 + fold * 18,
            102 + body_y,
            centre_x - 46 + fold * 20, 119, 30);
    }

    fpp_fill_ellipse(
        surface, centre_x, top + 52, 52, 56, 74);
    fpp_fill_ellipse(
        surface, centre_x, top + 50, 48, 52, 96);
    fpp_fill_ellipse(
        surface, centre_x, top + 54, 40, 46, 58);
    fpp_fill_ellipse(
        surface, centre_x, top + 52, 33, 40, 22);
    fpp_line(
        surface, centre_x - 44, top + 30,
        centre_x - 34, top + 78, 48);
    fpp_line(
        surface, centre_x + 44, top + 32,
        centre_x + 36, top + 80, 48);
    fpp_line(
        surface, centre_x - 20, top + 2,
        centre_x - 30, top + 20, 112);
    fpp_line(
        surface, centre_x + 20, top + 2,
        centre_x + 30, top + 20, 112);

    fpp_fill_ellipse(
        surface, centre_x, face_y + 4, 20, 24, 70);
    fpp_fill_ellipse(
        surface, centre_x - 2, face_y + 10, 17, 17, 118);
    fpp_fill_ellipse(
        surface, centre_x - 3, face_y + 16, 13, 10, 150);
    add_light(
        surface, centre_x - 4, face_y + 18,
        18, 12, 42 + pose->arousal / 8);

    const int gaze_x = pose->gaze_x / 24;
    const int gaze_y = pose->gaze_y / 40;
    for (int side = 0; side < 2; ++side) {
        const int eye_x =
            centre_x + (side == 0 ? -9 : 9);
        const int side_roll = side == 0 ? roll : -roll;
        const int openness =
            side == 0 ? pose->eye_open_left : pose->eye_open_right;
        const int height =
            fpp_clamp((openness * 4 + 127) / 255, 1, 4);
        fpp_fill_rect(
            surface, eye_x - 5,
            face_y - 3 + side_roll, 10, 7, 12);
        if (height > 1) {
            fpp_fill_rect(
                surface, eye_x - 3 + gaze_x,
                face_y - 1 + gaze_y + side_roll,
                6, height, 232);
            fpp_pixel(
                surface, eye_x - 3 + gaze_x,
                face_y - 1 + gaze_y + side_roll, 150);
        } else {
            fpp_hline(
                surface, eye_x - 4, eye_x + 4,
                face_y + side_roll, 220);
        }
    }
    draw_rogue_brow(
        surface, centre_x - 17, centre_x - 2,
        face_y - 7 + roll,
        pose->brow_outer_left, pose->brow_inner);
    draw_rogue_brow(
        surface, centre_x + 17, centre_x + 2,
        face_y - 7 - roll,
        pose->brow_outer_right, pose->brow_inner);

    fpp_vline(
        surface, centre_x, face_y + 5,
        face_y + 10, 60);
    fpp_pixel(
        surface, centre_x - 1, face_y + 10, 40);
    const int mouth_y = face_y + 15;
    const fpp_lips_t lips = {
        centre_x, mouth_y, 10, 13,
        38, 112, 10, 220, 88,
    };
    fpp_draw_polygon_lips(surface, &lips, pose);

    if (pose->cheek > 20) {
        const int glow = 24 + pose->cheek / 2;
        add_light(
            surface, centre_x - 15, face_y + 9,
            7, 5, glow);
        add_light(
            surface, centre_x + 15, face_y + 9,
            7, 5, glow);
    }
    for (int y = face_y - 8; y < face_y + 30; ++y) {
        for (int x = centre_x - 22;
             x <= centre_x + 22;
             x += 4) {
            const uint32_t hash =
                fpp_hash32(
                    (uint32_t)(y * 331 + x) ^ 0x64a1U);
            const int target_x = x + (int)(hash % 3U);
            if (target_x >= 0 && target_x < 160 &&
                y >= 0 && y < 120) {
                uint8_t *pixel =
                    &surface->pixels[
                        (size_t)y * 160U + (size_t)target_x];
                *pixel = (uint8_t)fpp_clamp(
                    *pixel + (int)(hash % 13U) - 6, 0, 255);
            }
        }
    }

    if (landmarks != NULL) {
        landmarks->face = (face_pixel_pack_bounds_t){
            (uint8_t)(centre_x - 55),
            (uint8_t)fpp_max(top - 2, 4),
            110, 106,
        };
        landmarks->left_eye = (face_pixel_pack_bounds_t){
            (uint8_t)(centre_x - 20),
            (uint8_t)(face_y - 13 + roll),
            22, 22,
        };
        landmarks->right_eye = (face_pixel_pack_bounds_t){
            (uint8_t)(centre_x - 2),
            (uint8_t)(face_y - 13 - roll),
            22, 22,
        };
        landmarks->mouth = (face_pixel_pack_bounds_t){
            (uint8_t)(centre_x - 16),
            (uint8_t)(mouth_y - 10),
            32, 28,
        };
    }
    fpp_dither_bayer_1bit(surface);

    /*
     * Ordered dither gives the hood its foggy pulp-print texture, but facial
     * signals must remain graphic and readable. Re-key the sockets, pupils,
     * brows, and lips in hard one-bit ink after dithering so the face keeps
     * its expression and viseme at actual 160x120 display size.
     */
    for (int side = 0; side < 2; ++side) {
        const int eye_x =
            centre_x + (side == 0 ? -10 : 10);
        const int side_roll = side == 0 ? roll : -roll;
        const int openness =
            side == 0 ? pose->eye_open_left : pose->eye_open_right;
        const int eye_height =
            fpp_clamp((openness * 7 + 127) / 255, 1, 7);
        fpp_fill_ellipse(
            surface, eye_x, face_y + side_roll,
            9, 7, 0U);
        if (eye_height > 1) {
            fpp_fill_ellipse(
                surface, eye_x,
                face_y + side_roll,
                7, eye_height, 1U);
            fpp_fill_ellipse(
                surface,
                eye_x + gaze_x,
                face_y + gaze_y + side_roll,
                2, fpp_min(eye_height, 3), 0U);
            fpp_pixel(
                surface, eye_x + gaze_x - 1,
                face_y + gaze_y - 1 + side_roll, 1U);
        } else {
            fpp_hline(
                surface, eye_x - 7, eye_x + 7,
                face_y + side_roll, 1U);
        }
    }
    const int left_outer_y =
        face_y - 10 + roll - pose->brow_outer_left / 12;
    const int left_inner_y =
        face_y - 10 + roll - pose->brow_inner / 12;
    const int right_outer_y =
        face_y - 10 - roll - pose->brow_outer_right / 12;
    const int right_inner_y =
        face_y - 10 - roll - pose->brow_inner / 12;
    fpp_line(
        surface, centre_x - 20, left_outer_y,
        centre_x - 3, left_inner_y, 1U);
    fpp_line(
        surface, centre_x - 20, left_outer_y + 1,
        centre_x - 3, left_inner_y + 1, 1U);
    fpp_line(
        surface, centre_x + 20, right_outer_y,
        centre_x + 3, right_inner_y, 1U);
    fpp_line(
        surface, centre_x + 20, right_outer_y + 1,
        centre_x + 3, right_inner_y + 1, 1U);

    fpp_fill_ellipse(
        surface, centre_x, mouth_y, 16, 11, 0U);
    const fpp_lips_t ink_lips = {
        centre_x, mouth_y, 13, 17,
        1U, 1U, 0U, 1U, 1U,
    };
    fpp_draw_polygon_lips(surface, &ink_lips, pose);
    if (pose->cheek > 20) {
        const int hatch_count =
            fpp_clamp(1 + pose->cheek / 58, 1, 4);
        for (int hatch = 0; hatch < hatch_count; ++hatch) {
            const int offset_y = face_y + 9 + hatch * 2;
            fpp_line(
                surface, centre_x - 24, offset_y + 2,
                centre_x - 18, offset_y, 1U);
            fpp_line(
                surface, centre_x + 18, offset_y,
                centre_x + 24, offset_y + 2, 1U);
        }
    }
    fpp_present(frame, surface, 1, 1, ROGUE_PALETTE);
}
