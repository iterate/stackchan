#include <M5Unified.h>

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "face_sprite_sheet.h"
#include "fspp_gameboy_bot_sticks3_chunky_atlas.h"
}

namespace {

constexpr char TAG[] = "sticks3-avatar";
constexpr uint16_t FRAME_WIDTH = 240;
constexpr uint16_t FRAME_HEIGHT = 135;
constexpr size_t FRAME_PIXELS =
    static_cast<size_t>(FRAME_WIDTH) * FRAME_HEIGHT;
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t MIC_SAMPLES = 320;
// A 4 ms conversion is zero ticks with the project's 100 Hz scheduler and
// therefore only yields to equal/higher-priority tasks. Keep this at a
// guaranteed tick so IDLE1 can feed the watchdog between display transfers.
constexpr TickType_t LOOP_DELAY = 1;
constexpr uint32_t RENDER_PERIOD_US = 33333;

struct ExpressionTarget {
    int8_t valence;
    uint8_t arousal;
    int8_t mouth_corner;
    int8_t brow_inner;
    uint8_t eye_squint;
};

constexpr ExpressionTarget EXPRESSIONS[] = {
    {0, 72, 0, 0, 0},        // neutral
    {52, 112, 36, 10, 20},   // warm
    {94, 184, 78, 22, 68},   // joy
    {-52, 126, -24, 58, 14}, // concern
    {18, 232, 8, 78, 0},     // surprise
    {4, 92, -6, 18, 22},     // thoughtful
    {-18, 104, -12, -14, 42},
    {12, 166, -8, -34, 54},
    {8, 34, 4, -26, 118},    // sleepy / blink
    {86, 250, 62, 52, 14},
    {28, 176, 24, 24, 116},
};
constexpr size_t EXPRESSION_COUNT =
    sizeof(EXPRESSIONS) / sizeof(EXPRESSIONS[0]);

uint16_t *frame = nullptr;
face_sprite_surface_t surface{};
face_sprite_player_t player{};
int16_t mic_samples[MIC_SAMPLES]{};
bool mic_pending = false;
bool ptt_previous = false;
uint8_t audio_level = 0;
size_t selected_expression = 0;
uint64_t next_render_us = 0;

uint32_t sample_clock(uint64_t now_us)
{
    return static_cast<uint32_t>(
        (now_us * SAMPLE_RATE) / 1000000ULL);
}

uint8_t measure_level(const int16_t *samples, size_t count)
{
    uint64_t absolute_sum = 0;
    for (size_t index = 0; index < count; ++index) {
        const int32_t value = samples[index];
        absolute_sum += static_cast<uint32_t>(
            value < 0 ? -value : value);
    }
    const uint32_t mean = static_cast<uint32_t>(
        absolute_sum / std::max<size_t>(count, 1));
    const uint32_t above_floor = mean > 70U ? mean - 70U : 0U;
    return static_cast<uint8_t>(
        std::min<uint32_t>(255U, above_floor * 255U / 1400U));
}

void update_microphone(bool ptt)
{
    if (ptt && !ptt_previous) {
        M5.Speaker.end();
        if (!M5.Mic.begin()) {
            ESP_LOGE(TAG, "microphone begin failed");
        }
        mic_pending = false;
        ESP_LOGI(TAG, "PTT start");
    }

    if (ptt) {
        if (mic_pending && M5.Mic.isRecording() == 0U) {
            const uint8_t measured =
                measure_level(mic_samples, MIC_SAMPLES);
            audio_level = static_cast<uint8_t>(
                (static_cast<uint16_t>(audio_level) * 3U + measured) / 4U);
            mic_pending = false;
        }
        if (!mic_pending &&
            M5.Mic.record(mic_samples, MIC_SAMPLES, SAMPLE_RATE)) {
            mic_pending = true;
        }
    } else {
        audio_level = static_cast<uint8_t>(
            static_cast<uint16_t>(audio_level) * 3U / 4U);
        if (ptt_previous) {
            M5.Mic.end();
            mic_pending = false;
            ESP_LOGI(TAG, "PTT end");
        }
    }
    ptt_previous = ptt;
}

void apply_expression(face_render_key_t *key, size_t index)
{
    const ExpressionTarget &expression =
        EXPRESSIONS[index % EXPRESSION_COUNT];
    key->affect_valence = expression.valence;
    key->affect_arousal = expression.arousal;
    key->mouth_corner_left = expression.mouth_corner;
    key->mouth_corner_right = expression.mouth_corner;
    key->brow_inner = expression.brow_inner;
    key->brow_outer_left = expression.brow_inner / 2;
    key->brow_outer_right = expression.brow_inner / 2;
    key->eye_left_squint = expression.eye_squint;
    key->eye_right_squint = expression.eye_squint;
    key->expression_weight = 255;
}

face_render_key_t make_render_key(
    uint32_t clock, bool ptt, bool speech_demo)
{
    face_render_key_t key{};
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.controls.eye_left_open = 255;
    key.controls.eye_right_open = 255;
    key.controls.mouth_width = 128;
    key.controls.expression =
        ptt ? FACE_ACTIVITY_LISTENING : FACE_ACTIVITY_IDLE;
    key.viseme = FACE_VISEME_NONE;
    key.viseme_secondary = FACE_VISEME_NONE;
    key.phoneme = FACE_PHONEME_NONE;
    key.audio_level = audio_level;
    key.attention = ptt ? 255 : 184;

    size_t expression = ptt ? 5U : selected_expression;
    const uint32_t blink_phase = clock % (SAMPLE_RATE * 5U);
    if (!ptt && !speech_demo &&
        blink_phase >= SAMPLE_RATE * 4U + 12000U &&
        blink_phase < SAMPLE_RATE * 4U + 14400U) {
        expression = 8U;
    }
    apply_expression(&key, expression);

    const uint32_t pose_phase = (clock / (SAMPLE_RATE * 3U)) % 4U;
    constexpr int8_t YAW[] = {-5, 2, 6, -2};
    key.head_yaw = YAW[pose_phase];
    key.head_pitch = ptt
        ? static_cast<int8_t>(-4 - audio_level / 64)
        : 0;

    if (speech_demo) {
        constexpr uint8_t VISEMES[] = {
            FACE_VISEME_PP,
            FACE_VISEME_AA,
            FACE_VISEME_E,
            FACE_VISEME_O,
            FACE_VISEME_FF,
            FACE_VISEME_I,
        };
        const size_t viseme_index =
            (clock / 2400U) %
            (sizeof(VISEMES) / sizeof(VISEMES[0]));
        key.viseme_set = FACE_VISEME_SET_OVR15;
        key.viseme = VISEMES[viseme_index];
        key.viseme_weight = 255;
        key.speech_phase = FACE_SPEECH_ACTIVE;
        key.controls.flags |= FACE_KEYFRAME_FLAG_SPEAKING;
        key.controls.mouth_open = 190;
        key.controls.mouth_width = 176;
        key.audio_level = 180;
    }
    return key;
}

uint32_t frame_hash()
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0; index < FRAME_PIXELS; ++index) {
        hash ^= frame[index];
        hash *= 16777619U;
    }
    return hash;
}

bool initialise()
{
    auto config = M5.config();
    config.clear_display = true;
    config.internal_mic = true;
    config.internal_spk = true;
    config.internal_imu = false;
    config.internal_rtc = false;
    M5.begin(config);

    M5.Speaker.end();
    M5.Display.setColorDepth(16);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(160);
    M5.Display.setSwapBytes(true);

    ESP_LOGI(
        TAG, "board=%d display=%" PRId32 "x%" PRId32,
        static_cast<int>(M5.getBoard()),
        M5.Display.width(), M5.Display.height());
    if (M5.getBoard() != m5::board_t::board_M5StickS3 ||
        M5.Display.width() != FRAME_WIDTH ||
        M5.Display.height() != FRAME_HEIGHT) {
        ESP_LOGE(TAG, "expected M5StickS3 with 240x135 landscape display");
        M5.Display.fillScreen(TFT_RED);
        return false;
    }

    frame = static_cast<uint16_t *>(heap_caps_malloc(
        FRAME_PIXELS * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (frame == nullptr) {
        frame = static_cast<uint16_t *>(heap_caps_malloc(
            FRAME_PIXELS * sizeof(uint16_t), MALLOC_CAP_8BIT));
    }
    if (frame == nullptr) {
        ESP_LOGE(TAG, "frame allocation failed (%zu bytes)",
                 FRAME_PIXELS * sizeof(uint16_t));
        M5.Display.fillScreen(TFT_RED);
        return false;
    }

    surface.pixels = frame;
    surface.pixel_capacity = FRAME_PIXELS;
    surface.width = FRAME_WIDTH;
    surface.height = FRAME_HEIGHT;
    surface.stride = FRAME_WIDTH;
    if (!face_sprite_player_init(
            &player,
            &face_sprite_fspp_gameboy_bot_sticks3_chunky_atlas_dark)) {
        ESP_LOGE(TAG, "atlas validation failed");
        M5.Display.fillScreen(TFT_RED);
        return false;
    }
    ESP_LOGI(
        TAG,
        "ready atlas=%s native=%ux%u scale=%u frame_bytes=%zu",
        player.atlas->name,
        player.atlas->native_width,
        player.atlas->native_height,
        player.atlas->scale,
        FRAME_PIXELS * sizeof(uint16_t));
    return true;
}

void app_loop()
{
    M5.update();
    const bool ptt = M5.BtnA.isPressed();
    const bool speech_demo = M5.BtnB.isPressed();
    if (M5.BtnB.wasClicked()) {
        selected_expression =
            (selected_expression + 1U) % EXPRESSION_COUNT;
        ESP_LOGI(TAG, "expression=%zu", selected_expression);
    }
    update_microphone(ptt);
    M5.Power.setLed(ptt ? 96 : 0);

    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if (now_us >= next_render_us) {
        const uint32_t clock = sample_clock(now_us);
        const face_render_key_t key =
            make_render_key(clock, ptt, speech_demo);
        if (!face_sprite_render_to(&player, &key, clock, &surface)) {
            ESP_LOGE(TAG, "render failed");
        } else {
            M5.Display.pushImage(
                0, 0, FRAME_WIDTH, FRAME_HEIGHT, frame);
        }
        next_render_us = now_us + RENDER_PERIOD_US;

        static uint64_t next_evidence_us = 0;
        if (now_us >= next_evidence_us) {
            ESP_LOGI(
                TAG,
                "frame hash=%08" PRIx32
                " ptt=%d demo=%d level=%u heap=%" PRIu32
                " psram=%" PRIu32,
                frame_hash(),
                ptt,
                speech_demo,
                audio_level,
                static_cast<uint32_t>(heap_caps_get_free_size(
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<uint32_t>(heap_caps_get_free_size(
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
            next_evidence_us = now_us + 2000000ULL;
        }
    }
    vTaskDelay(LOOP_DELAY);
}

void task_main(void *)
{
    if (!initialise()) {
        vTaskDelete(nullptr);
        return;
    }
    for (;;) {
        app_loop();
    }
}

} // namespace

extern "C" void app_main(void)
{
    xTaskCreatePinnedToCore(
        task_main, "sticks3-avatar", 8192, nullptr, 2, nullptr, 1);
}
