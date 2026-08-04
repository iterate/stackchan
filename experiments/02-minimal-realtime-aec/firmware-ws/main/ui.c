#include "ui.h"

#include <stdbool.h>
#include <string.h>

#include "audio_pipeline.h"
#include "bsp/display.h"
#include "bsp/m5stack_core_s3.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "face_geometry.h"
#include "lvgl.h"

static const char *TAG = "ui";

static lv_display_t *s_display;
static lv_obj_t *s_title;
static lv_obj_t *s_detail;
static lv_obj_t *s_button_label;
static lv_obj_t *s_left_eye;
static lv_obj_t *s_right_eye;
static lv_obj_t *s_left_pupil;
static lv_obj_t *s_right_pupil;
static lv_obj_t *s_mouth;
static ui_tap_callback_t s_tap_callback;
static face_animator_state_t s_last_face;
static bool s_have_last_face;

static void tap_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && s_tap_callback != NULL) {
        s_tap_callback();
    }
}

static lv_obj_t *create_face_part(lv_obj_t *parent, uint32_t colour)
{
    lv_obj_t *part = lv_obj_create(parent);
    lv_obj_remove_flag(part, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(part, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(part, 0, 0);
    lv_obj_set_style_border_width(part, 0, 0);
    lv_obj_set_style_radius(part, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(part, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
    return part;
}

static bool same_face(const face_animator_state_t *left,
                      const face_animator_state_t *right)
{
    return left->mouth_open == right->mouth_open &&
           left->mouth_width == right->mouth_width &&
           left->eye_open == right->eye_open &&
           left->gaze_x == right->gaze_x &&
           left->gaze_y == right->gaze_y;
}

static void position_pupil(lv_obj_t *pupil,
                           const face_geometry_t *geometry)
{
    if (geometry->pupil_size == 0) {
        lv_obj_add_flag(pupil, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(pupil, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(
        pupil, geometry->pupil_size, geometry->pupil_size);
    lv_obj_set_pos(
        pupil,
        ((int32_t)geometry->eye_width - geometry->pupil_size) / 2 +
            geometry->pupil_offset_x,
        ((int32_t)geometry->eye_height - geometry->pupil_size) / 2 +
            geometry->pupil_offset_y);
}

static void face_timer(lv_timer_t *timer)
{
    (void)timer;
    face_animator_state_t state;
    audio_pipeline_face_snapshot(&state);
    if (s_have_last_face && same_face(&state, &s_last_face)) {
        return;
    }

    face_geometry_t geometry;
    face_geometry_from_state(
        &state, BSP_LCD_H_RES, BSP_LCD_V_RES, &geometry);
    lv_obj_set_pos(
        s_left_eye, geometry.left_eye_x, geometry.left_eye_y);
    lv_obj_set_size(
        s_left_eye, geometry.eye_width, geometry.eye_height);
    lv_obj_set_pos(
        s_right_eye, geometry.right_eye_x, geometry.right_eye_y);
    lv_obj_set_size(
        s_right_eye, geometry.eye_width, geometry.eye_height);
    position_pupil(s_left_pupil, &geometry);
    position_pupil(s_right_pupil, &geometry);
    lv_obj_set_pos(s_mouth, geometry.mouth_x, geometry.mouth_y);
    lv_obj_set_size(
        s_mouth, geometry.mouth_width, geometry.mouth_height);

    s_last_face = state;
    s_have_last_face = true;
}

esp_err_t ui_init(ui_tap_callback_t tap_callback)
{
    s_tap_callback = tap_callback;
    s_display = bsp_display_start();
    if (s_display == NULL) {
        ESP_LOGE(TAG, "BSP display start failed");
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_backlight_on());
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_brightness_set(80));

    if (!bsp_display_lock(1000)) {
        ESP_LOGE(TAG, "Timed out acquiring LVGL lock");
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071521), 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, tap_event, LV_EVENT_CLICKED, NULL);

    s_left_eye = create_face_part(screen, 0xF4F8FF);
    s_right_eye = create_face_part(screen, 0xF4F8FF);
    s_left_pupil = create_face_part(s_left_eye, 0x071521);
    s_right_pupil = create_face_part(s_right_eye, 0x071521);
    s_mouth = create_face_part(screen, 0xFF7B72);

    s_title = lv_label_create(screen);
    lv_label_set_text(s_title, "BOOTING");
    lv_obj_set_style_text_color(s_title, lv_color_hex(0x60D394), 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 8);

    s_detail = lv_label_create(screen);
    lv_obj_set_width(s_detail, 300);
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_detail, lv_color_hex(0xB7D9F7), 0);
    lv_label_set_text(s_detail, "Booting audio...");
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_DOT);
    lv_obj_align(s_detail, LV_ALIGN_BOTTOM_MID, 0, -25);

    s_button_label = lv_label_create(screen);
    lv_label_set_text(s_button_label, "Always listening");
    lv_obj_set_style_text_color(
        s_button_label, lv_color_hex(0x6D879C), 0);
    lv_obj_align(s_button_label, LV_ALIGN_BOTTOM_MID, 0, -7);

    (void)lv_timer_create(face_timer, 33, NULL);
    face_timer(NULL);

    bsp_display_unlock();
    ESP_LOGI(TAG, "Animated face and touch ready");
    return ESP_OK;
}

void ui_set_status(const char *title, const char *detail)
{
    if (s_display == NULL || s_title == NULL || s_detail == NULL) {
        return;
    }
    if (!bsp_display_lock(100)) {
        ESP_LOGW(TAG, "Dropped UI update while display was busy");
        return;
    }
    lv_label_set_text(s_title, title ? title : "");
    lv_label_set_text(s_detail, detail ? detail : "");
    bsp_display_unlock();
}

void ui_set_button_label(const char *label)
{
    if (s_display == NULL || s_button_label == NULL) {
        return;
    }
    if (!bsp_display_lock(100)) {
        ESP_LOGW(TAG, "Dropped button update while display was busy");
        return;
    }
    lv_label_set_text(s_button_label, label ? label : "");
    bsp_display_unlock();
}

esp_err_t ui_snapshot_rgb565(ui_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (s_display == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t width = BSP_LCD_H_RES;
    const uint32_t height = BSP_LCD_V_RES;
    const uint32_t stride =
        lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    const size_t bytes = (size_t)stride * height;
    uint8_t *pixels = heap_caps_aligned_alloc(
        64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pixels == NULL) {
        ESP_LOGE(TAG, "Unable to allocate %u-byte screen snapshot",
                 (unsigned)bytes);
        return ESP_ERR_NO_MEM;
    }

    if (!bsp_display_lock(2000)) {
        heap_caps_free(pixels);
        return ESP_ERR_TIMEOUT;
    }

    lv_draw_buf_t draw_buffer;
    lv_result_t result = lv_draw_buf_init(
        &draw_buffer, width, height, LV_COLOR_FORMAT_RGB565, stride,
        pixels, bytes);
    if (result == LV_RESULT_OK) {
        result = lv_snapshot_take_to_draw_buf(
            lv_screen_active(), LV_COLOR_FORMAT_RGB565, &draw_buffer);
    }
    bsp_display_unlock();

    if (result != LV_RESULT_OK) {
        ESP_LOGE(TAG, "LVGL screen snapshot failed");
        heap_caps_free(pixels);
        return ESP_FAIL;
    }

    snapshot->pixels = pixels;
    snapshot->width = draw_buffer.header.w;
    snapshot->height = draw_buffer.header.h;
    snapshot->stride = draw_buffer.header.stride;
    snapshot->bytes = draw_buffer.data_size;
    return ESP_OK;
}

void ui_snapshot_release(ui_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    heap_caps_free(snapshot->pixels);
    memset(snapshot, 0, sizeof(*snapshot));
}
