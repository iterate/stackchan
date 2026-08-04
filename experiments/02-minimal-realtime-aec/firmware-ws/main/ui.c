#include "ui.h"

#include <stdbool.h>
#include <string.h>

#include "audio_pipeline.h"
#include "bsp/display.h"
#include "bsp/m5stack_core_s3.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "face_avatar_registry.h"
#include "face_keyframe.h"
#include "face_render.h"
#include "lvgl.h"

static const char *TAG = "ui";

static lv_display_t *s_display;
static lv_obj_t *s_face_canvas;
static lv_obj_t *s_title;
static lv_obj_t *s_detail;
static lv_obj_t *s_button_label;
static lv_obj_t *s_renderer_label;
static uint16_t *s_face_pixels;
static ui_tap_callback_t s_tap_callback;
static bool s_render_error_logged;
static bool s_face_only;
static size_t s_renderer_index = SIZE_MAX;

static void tap_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && s_tap_callback != NULL) {
        s_tap_callback();
    }
}

static void style_overlay_label(lv_obj_t *label, uint32_t text_colour)
{
    lv_obj_set_style_text_color(label, lv_color_hex(text_colour), 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(0x071521), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_60, 0);
    lv_obj_set_style_pad_left(label, 4, 0);
    lv_obj_set_style_pad_right(label, 4, 0);
    lv_obj_set_style_pad_top(label, 2, 0);
    lv_obj_set_style_pad_bottom(label, 2, 0);
    lv_obj_set_style_radius(label, 3, 0);
}

static void set_overlay_hidden(lv_obj_t *label, bool hidden)
{
    if (label == NULL) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_face_only(void)
{
    set_overlay_hidden(s_title, s_face_only);
    set_overlay_hidden(s_detail, s_face_only);
    set_overlay_hidden(s_button_label, s_face_only);
    set_overlay_hidden(s_renderer_label, s_face_only);
}

static void face_timer(lv_timer_t *timer)
{
    (void)timer;
    face_animator_state_t state;
    audio_pipeline_face_snapshot(&state);

    face_render_key_t render_key;
    face_render_key_from_pose(&state, &render_key);
    if (!face_avatar_registry_render(
            &render_key,
            state.playout_samples,
            s_face_pixels,
            FACE_RENDER_PIXEL_COUNT)) {
        if (!s_render_error_logged) {
            ESP_LOGE(
                TAG, "Shared sprite renderer failed for avatar=%s",
                face_avatar_registry_current_slug());
            s_render_error_logged = true;
        }
        return;
    }
    s_render_error_logged = false;
    const size_t renderer_index =
        face_avatar_registry_current_index();
    if (renderer_index != s_renderer_index) {
        s_renderer_index = renderer_index;
        lv_label_set_text(
            s_renderer_label,
            face_avatar_registry_current_name());
        ESP_LOGI(
            TAG,
            "Avatar %u/%u: %s",
            (unsigned)(renderer_index + 1U),
            (unsigned)face_avatar_registry_count(),
            face_avatar_registry_current_slug());
    }
    lv_obj_invalidate(s_face_canvas);
}

esp_err_t ui_init(ui_tap_callback_t tap_callback)
{
    s_tap_callback = tap_callback;
    if (!face_avatar_registry_init()) {
        ESP_LOGE(TAG, "No valid sprite avatar atlas");
        return ESP_ERR_INVALID_STATE;
    }
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
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, tap_event, LV_EVENT_CLICKED, NULL);

    s_face_pixels = heap_caps_aligned_alloc(
        64, FACE_RENDER_FRAME_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_face_pixels == NULL) {
        bsp_display_unlock();
        ESP_LOGE(
            TAG, "Unable to allocate %u-byte shared face framebuffer",
            (unsigned)FACE_RENDER_FRAME_BYTES);
        return ESP_ERR_NO_MEM;
    }

    s_face_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(
        s_face_canvas,
        s_face_pixels,
        FACE_RENDER_WIDTH,
        FACE_RENDER_HEIGHT,
        LV_COLOR_FORMAT_RGB565);
    lv_image_set_scale(s_face_canvas, 512);
    lv_image_set_antialias(s_face_canvas, false);
    lv_obj_center(s_face_canvas);
    lv_obj_remove_flag(s_face_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_face_canvas, LV_OBJ_FLAG_SCROLLABLE);

    s_title = lv_label_create(screen);
    lv_label_set_text(s_title, "BOOTING");
    style_overlay_label(s_title, 0x60D394);
    lv_obj_align(s_title, LV_ALIGN_TOP_RIGHT, -4, 4);

    s_detail = lv_label_create(screen);
    lv_obj_set_width(s_detail, 300);
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
    style_overlay_label(s_detail, 0xB7D9F7);
    lv_label_set_text(s_detail, "Booting audio...");
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_DOT);
    lv_obj_align(s_detail, LV_ALIGN_BOTTOM_MID, 0, -25);

    s_button_label = lv_label_create(screen);
    lv_label_set_text(s_button_label, "Always listening");
    style_overlay_label(s_button_label, 0x6D879C);
    lv_obj_align(s_button_label, LV_ALIGN_BOTTOM_MID, 0, -7);

    s_renderer_label = lv_label_create(screen);
    lv_label_set_text(
        s_renderer_label,
        face_avatar_registry_current_name());
    style_overlay_label(s_renderer_label, 0xD7F171);
    lv_obj_align(s_renderer_label, LV_ALIGN_TOP_LEFT, 4, 4);
    apply_face_only();

    (void)lv_timer_create(face_timer, 33, NULL);
    face_timer(NULL);

    bsp_display_unlock();
    ESP_LOGI(
        TAG,
        "Shared C sprite renderer ready: avatar=%s count=%u "
        "frame=%ux%u bytes=%u PSRAM_free=%u",
        face_avatar_registry_current_slug(),
        (unsigned)face_avatar_registry_count(),
        FACE_RENDER_WIDTH,
        FACE_RENDER_HEIGHT,
        (unsigned)FACE_RENDER_FRAME_BYTES,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return ESP_OK;
}

void ui_set_face_only(bool face_only)
{
    if (s_display == NULL) {
        return;
    }
    if (!bsp_display_lock(100)) {
        ESP_LOGW(TAG, "Dropped overlay update while display was busy");
        return;
    }
    s_face_only = face_only;
    apply_face_only();
    bsp_display_unlock();
}

esp_err_t ui_next_avatar(void)
{
    if (s_display == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!bsp_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }
    const bool selected = face_avatar_registry_next();
    if (selected) {
        face_timer(NULL);
    }
    bsp_display_unlock();
    return selected ? ESP_OK : ESP_FAIL;
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
