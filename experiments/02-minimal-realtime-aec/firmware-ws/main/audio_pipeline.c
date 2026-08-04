#include "audio_pipeline.h"

#include <string.h>

#include "app_config.h"
#include "app_status.h"
#include "audio_trace.h"
#include "bsp/m5stack_core_s3.h"
#include "diagnostics.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_aec.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "face_animator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "speech_leveler.h"

#define PLAYBACK_BUFFER_BYTES \
    (STACKCHAN_AUDIO_SAMPLE_RATE * sizeof(int16_t) * 4)
#define CLEAN_BUFFER_BYTES \
    (STACKCHAN_AUDIO_SAMPLE_RATE * sizeof(int16_t) * \
     STACKCHAN_CLEAN_AUDIO_BUFFER_MS / 1000)
#define PLAYBACK_STORAGE_BYTES (PLAYBACK_BUFFER_BYTES + 1)
#define CLEAN_STORAGE_BYTES (CLEAN_BUFFER_BYTES + 1)
#define STACKCHAN_AEC_MODE AEC_MODE_FD_HIGH_PERF
#define STACKCHAN_AEC_MODE_NAME "fd_high_perf"
#define STACKCHAN_I2S_TAP_SAMPLES 128
#define STACKCHAN_TDM_CAPTURE_CHANNELS 4
#define STACKCHAN_TDM_NEAR_INDEX 0
#define STACKCHAN_TDM_REFERENCE_INDEX 1
#define STACKCHAN_AEC_REFERENCE_GAIN_DB 0
#define STACKCHAN_AW88298_I2SCTRL_REG 0x06
#define STACKCHAN_AW88298_I2SBCK_MASK 0x30
#define STACKCHAN_AW88298_I2SBCK_64FS 0x20
/*
 * Eight completed 8 ms DMA chunks cover two complete 32 ms AEC frames.
 * A 16-entry queue retained 128 ms of stale audio in internal SRAM and could
 * itself mask scheduling problems. The AEC task has higher priority than the
 * UI/network tasks; drops/skips remain observable in the timing endpoint.
 */
#define STACKCHAN_I2S_TAP_QUEUE_DEPTH 8

static const char *TAG = "audio";

typedef struct {
    uint32_t sequence;
    uint16_t sample_count;
    uint16_t reserved;
    int16_t pcm[STACKCHAN_I2S_TAP_SAMPLES];
} audio_tx_dma_chunk_t;

typedef struct {
    uint32_t sequence;
    uint16_t sample_count;
    uint16_t reserved;
    int16_t near[STACKCHAN_I2S_TAP_SAMPLES];
    int16_t hardware_reference[STACKCHAN_I2S_TAP_SAMPLES];
} audio_rx_dma_chunk_t;

static esp_codec_dev_handle_t s_speaker;
static esp_codec_dev_handle_t s_microphone;
static aec_handle_t *s_aec;
static size_t s_frame_samples;
static StreamBufferHandle_t s_playback;
static StreamBufferHandle_t s_clean;
static StaticStreamBuffer_t s_playback_control;
static StaticStreamBuffer_t s_clean_control;
static uint8_t *s_playback_storage;
static uint8_t *s_clean_storage;
static int16_t *s_raw_frame;
static int16_t *s_reference_frame;
static int16_t *s_hardware_reference_frame;
static int16_t *s_playback_frame;
static int16_t *s_drain_frame;
static int16_t *s_aec_reference_frame;
static int16_t *s_clean_frame;
static int16_t *s_reference_delay_storage;
static TaskHandle_t s_audio_task;
static TaskHandle_t s_aec_task;
static QueueHandle_t s_tx_tap_queue;
static QueueHandle_t s_rx_tap_queue;
static StaticQueue_t s_tx_tap_queue_control;
static StaticQueue_t s_rx_tap_queue_control;
static uint8_t s_tx_tap_queue_storage[
    STACKCHAN_I2S_TAP_QUEUE_DEPTH * sizeof(audio_tx_dma_chunk_t)]
    __attribute__((aligned(portBYTE_ALIGNMENT)));
static uint8_t s_rx_tap_queue_storage[
    STACKCHAN_I2S_TAP_QUEUE_DEPTH * sizeof(audio_rx_dma_chunk_t)]
    __attribute__((aligned(portBYTE_ALIGNMENT)));
static DRAM_ATTR audio_tx_dma_chunk_t s_tx_isr_chunk;
static DRAM_ATTR audio_rx_dma_chunk_t s_rx_isr_chunk;
static DRAM_ATTR uint32_t
    s_tdm_slot_peaks[STACKCHAN_TDM_CAPTURE_CHANNELS];
static bool s_tap_enabled;
static uint32_t s_tap_tx_drops;
static uint32_t s_tap_rx_drops;
static uint32_t s_tap_sequence_skips;
static uint32_t s_tap_pairs;
static uint32_t s_latest_write_us;
static uint32_t s_latest_read_us;
static size_t s_reference_delay_write_index;
static int s_active_reference_delay_samples = -1;
static bool s_ready;
static int s_speaker_volume = STACKCHAN_SPEAKER_VOLUME;
static int s_playback_gain_db = STACKCHAN_PLAYBACK_GAIN_DB;
static int s_microphone_gain_db = STACKCHAN_MIC_GAIN_DB;
static int s_aec_reference_delay_ms = STACKCHAN_AEC_REFERENCE_DELAY_MS;
static int s_aec_nlp_level = STACKCHAN_AEC_NLP_LEVEL;
static int s_active_aec_nlp_level = STACKCHAN_AEC_NLP_LEVEL;
static uint32_t s_level_processed_samples;
static uint32_t s_level_limited_samples;
static uint32_t s_level_overrange_samples;
static uint32_t s_level_source_peak;
static uint32_t s_level_pre_limiter_peak;
static uint32_t s_level_output_peak;
static uint32_t s_timing_epoch;
static uint32_t s_timing_frames;
static uint32_t s_timing_over_budget_frames;
static uint32_t s_timing_last_frame_us;
static uint32_t s_timing_maximum_frame_us;
static uint32_t s_timing_last_write_us;
static uint32_t s_timing_maximum_write_us;
static uint32_t s_timing_last_read_us;
static uint32_t s_timing_maximum_read_us;
static uint32_t s_timing_last_reference_us;
static uint32_t s_timing_maximum_reference_us;
static uint32_t s_timing_last_aec_us;
static uint32_t s_timing_maximum_aec_us;
static uint32_t s_timing_last_aec_linear_us;
static uint32_t s_timing_maximum_aec_linear_us;
static uint32_t s_timing_last_aec_nlp_us;
static uint32_t s_timing_maximum_aec_nlp_us;
static uint32_t s_timing_total_frame_us;
static uint32_t s_timing_total_write_us;
static uint32_t s_timing_total_read_us;
static uint32_t s_timing_total_reference_us;
static uint32_t s_timing_total_aec_us;
static uint32_t s_timing_total_aec_linear_us;
static uint32_t s_timing_total_aec_nlp_us;
static face_animator_t s_face_animator;
static bool s_face_ready;

/*
 * The CoreS3 BSP declares 15 dB of fixed AW88298 PA gain. esp_codec_dev
 * subtracts that gain from its requested route gain, so its stock 0 dB
 * maximum leaves the codec itself at -15 dB. M5Stack's own CoreS3 driver
 * instead programs the AW88298 to 0 dB (register 0x0c = 0x0064).
 *
 * Extend the logical curve by the declared PA gain so 100% reaches the same
 * codec setting as M5Stack's driver. Volume 1 remains at the stock -49.5 dB
 * codec level and volume 0 retains esp_codec_dev's hard mute behavior.
 */
static esp_codec_dev_vol_map_t s_speaker_volume_map[] = {
    {.vol = 1, .db_value = -34.5f},
    {.vol = 100, .db_value = 15.0f},
};

static int16_t *aligned_audio_buffer(size_t samples)
{
    return heap_caps_aligned_alloc(
        16, samples * sizeof(int16_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

/*
 * Four 16-bit ES7210 TDM slots make the shared full-duplex I2S peripheral
 * generate 64 BCLKs per LRCK frame. The AW88298 defaults to 32 BCLKs and
 * consequently rejects the otherwise-valid speaker stream. Keep its ordinary
 * Philips-I2S input mode, but explicitly select the documented 64*fs BCK mode.
 */
static esp_err_t configure_speaker_for_shared_tdm_clock(void)
{
    int i2s_control = 0;
    if (esp_codec_dev_read_reg(
            s_speaker, STACKCHAN_AW88298_I2SCTRL_REG,
            &i2s_control) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    i2s_control =
        (i2s_control & ~STACKCHAN_AW88298_I2SBCK_MASK) |
        STACKCHAN_AW88298_I2SBCK_64FS;
    if (esp_codec_dev_write_reg(
            s_speaker, STACKCHAN_AW88298_I2SCTRL_REG,
            i2s_control) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }

    int verified = 0;
    if (esp_codec_dev_read_reg(
            s_speaker, STACKCHAN_AW88298_I2SCTRL_REG,
            &verified) != ESP_CODEC_DEV_OK ||
        (verified & STACKCHAN_AW88298_I2SBCK_MASK) !=
            STACKCHAN_AW88298_I2SBCK_64FS) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "AW88298 I2S control=0x%04x (64 BCLK/frame)",
             verified);
    return ESP_OK;
}

static void atomic_note_peak(uint32_t *peak, uint32_t candidate)
{
    uint32_t previous = __atomic_load_n(peak, __ATOMIC_RELAXED);
    while (candidate > previous &&
           !__atomic_compare_exchange_n(
               peak, &previous, candidate, false,
               __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void record_audio_timing(uint32_t epoch, uint32_t frame_us,
                                uint32_t write_us, uint32_t read_us,
                                uint32_t reference_us, uint32_t aec_us,
                                uint32_t aec_linear_us,
                                uint32_t aec_nlp_us)
{
    if (__atomic_load_n(&s_timing_epoch, __ATOMIC_RELAXED) != epoch) {
        return;
    }
    const uint32_t budget_us =
        s_frame_samples * 1000000U / STACKCHAN_AUDIO_SAMPLE_RATE;
    __atomic_fetch_add(&s_timing_frames, 1, __ATOMIC_RELAXED);
    if (frame_us > budget_us + 1000U) {
        __atomic_fetch_add(
            &s_timing_over_budget_frames, 1, __ATOMIC_RELAXED);
    }
    __atomic_store_n(
        &s_timing_last_frame_us, frame_us, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_last_write_us, write_us, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_last_read_us, read_us, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_last_reference_us, reference_us, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_last_aec_us, aec_us, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_last_aec_linear_us, aec_linear_us,
        __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_last_aec_nlp_us, aec_nlp_us, __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &s_timing_total_frame_us, frame_us, __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &s_timing_total_write_us, write_us, __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &s_timing_total_read_us, read_us, __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &s_timing_total_reference_us, reference_us, __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &s_timing_total_aec_us, aec_us, __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &s_timing_total_aec_linear_us, aec_linear_us,
        __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &s_timing_total_aec_nlp_us, aec_nlp_us, __ATOMIC_RELAXED);
    atomic_note_peak(&s_timing_maximum_frame_us, frame_us);
    atomic_note_peak(&s_timing_maximum_write_us, write_us);
    atomic_note_peak(&s_timing_maximum_read_us, read_us);
    atomic_note_peak(&s_timing_maximum_reference_us, reference_us);
    atomic_note_peak(&s_timing_maximum_aec_us, aec_us);
    atomic_note_peak(
        &s_timing_maximum_aec_linear_us, aec_linear_us);
    atomic_note_peak(&s_timing_maximum_aec_nlp_us, aec_nlp_us);
}

static void level_playback(int16_t *samples, size_t sample_count)
{
    speech_leveler_metrics_t frame;
    const int gain_db =
        __atomic_load_n(&s_playback_gain_db, __ATOMIC_RELAXED);
    speech_leveler_process(samples, sample_count, gain_db, &frame);
    __atomic_fetch_add(
        &s_level_processed_samples, frame.processed_samples,
        __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &s_level_limited_samples, frame.limited_samples,
        __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &s_level_overrange_samples, frame.overrange_samples,
        __ATOMIC_RELAXED);
    atomic_note_peak(&s_level_source_peak, frame.source_peak);
    atomic_note_peak(&s_level_pre_limiter_peak, frame.pre_limiter_peak);
    atomic_note_peak(&s_level_output_peak, frame.output_peak);
}

/*
 * This delays only the signal presented to the AEC. It never delays audible
 * playback. The exact, undelayed PCM written to the speaker remains in
 * s_reference_frame and in every diagnostic capture.
 */
static void prepare_aec_reference(const int16_t *speaker_reference,
                                  size_t sample_count)
{
    const int delay_ms = __atomic_load_n(
        &s_aec_reference_delay_ms, __ATOMIC_RELAXED);
    const int delay_samples =
        delay_ms * STACKCHAN_AUDIO_SAMPLE_RATE / 1000;

    if (delay_samples != s_active_reference_delay_samples) {
        const size_t maximum_delay_samples =
            STACKCHAN_AEC_REFERENCE_DELAY_MAX_MS *
            STACKCHAN_AUDIO_SAMPLE_RATE / 1000;
        memset(s_reference_delay_storage, 0,
               maximum_delay_samples * sizeof(int16_t));
        s_reference_delay_write_index = 0;
        s_active_reference_delay_samples = delay_samples;
    }

    if (delay_samples == 0) {
        memcpy(s_aec_reference_frame, speaker_reference,
               sample_count * sizeof(int16_t));
        return;
    }

    for (size_t index = 0; index < sample_count; ++index) {
        s_aec_reference_frame[index] =
            s_reference_delay_storage[s_reference_delay_write_index];
        s_reference_delay_storage[s_reference_delay_write_index] =
            speaker_reference[index];
        s_reference_delay_write_index++;
        if (s_reference_delay_write_index == (size_t)delay_samples) {
            s_reference_delay_write_index = 0;
        }
    }
}

static void log_heap(const char *stage)
{
    ESP_LOGI(
        TAG,
        "%s: internal free=%u largest=%u, PSRAM free=%u largest=%u",
        stage,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                          MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                   MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM |
                                          MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM |
                                                   MALLOC_CAP_8BIT));
}

static esp_err_t initialise_codecs(void)
{
    /*
     * Match M5Stack/xiaozhi's known-good CoreS3 clocking exactly. The
     * AW88298 consumes ordinary stereo I2S while the ES7210 emits TDM.
     */
    const i2s_std_config_t tx_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(STACKCHAN_AUDIO_SAMPLE_RATE),
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    const i2s_tdm_config_t rx_config = {
        .clk_cfg = {
            .sample_rate_hz = STACKCHAN_AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask =
                I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = I2S_GPIO_UNUSED,
            .din = BSP_I2S_DSIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "Initialize CoreS3 I2C");
    ESP_RETURN_ON_ERROR(
        bsp_audio_init_tdm_rx(&tx_config, &rx_config), TAG,
        "Initialize CoreS3 standard TX / TDM RX");

    s_speaker = bsp_audio_codec_speaker_init();
    s_microphone = bsp_audio_codec_microphone_init();
    if (s_speaker == NULL || s_microphone == NULL) {
        ESP_LOGE(TAG, "CoreS3 speaker or microphone codec creation failed");
        return ESP_FAIL;
    }

    esp_codec_dev_vol_curve_t volume_curve = {
        .vol_map = s_speaker_volume_map,
        .count = sizeof(s_speaker_volume_map) /
                 sizeof(s_speaker_volume_map[0]),
    };
    esp_codec_dev_sample_info_t speaker_format = {
        .sample_rate = STACKCHAN_AUDIO_SAMPLE_RATE,
        .channel = STACKCHAN_AUDIO_CHANNELS,
        .bits_per_sample = STACKCHAN_AUDIO_BITS,
    };
    /*
     * Observe all four logical TDM slots while validating the actual order on
     * this CoreS3 revision. MIC3 is wired to an analogue divider across the
     * speaker output and is therefore clock-synchronous with the near mic.
     * Gain masks below use physical microphone numbering, not TDM order:
     * bit 0 = MIC1, bit 2 = MIC3.
     */
    esp_codec_dev_sample_info_t microphone_format = {
        .sample_rate = STACKCHAN_AUDIO_SAMPLE_RATE,
        .channel = 4,
        .channel_mask =
            ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
            ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1) |
            ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2) |
            ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3),
        .bits_per_sample = STACKCHAN_AUDIO_BITS,
    };
    if (esp_codec_dev_set_vol_curve(s_speaker, &volume_curve) != ESP_OK ||
        esp_codec_dev_set_out_vol(
            s_speaker, s_speaker_volume) != ESP_OK ||
        esp_codec_dev_open(s_speaker, &speaker_format) != ESP_OK ||
        esp_codec_dev_open(s_microphone, &microphone_format) != ESP_OK ||
        configure_speaker_for_shared_tdm_clock() != ESP_OK ||
        esp_codec_dev_set_in_channel_gain(
            s_microphone, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
            (float)s_microphone_gain_db) != ESP_OK ||
        esp_codec_dev_set_in_channel_gain(
            s_microphone, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2),
            STACKCHAN_AEC_REFERENCE_GAIN_DB) != ESP_OK) {
        ESP_LOGE(TAG, "CoreS3 codec configuration failed");
        return ESP_FAIL;
    }
    ESP_LOGI(
        TAG,
        "CoreS3 capture: TDM slot 0 MIC1 near=%d dB, "
        "slot 1 MIC3 speaker reference=%d dB",
        s_microphone_gain_db, STACKCHAN_AEC_REFERENCE_GAIN_DB);
    return ESP_OK;
}

static bool IRAM_ATTR audio_i2s_tap(
    bool transmit, uint32_t sequence, const void *pcm, size_t bytes,
    void *user_data)
{
    (void)user_data;
    if (!__atomic_load_n(&s_tap_enabled, __ATOMIC_ACQUIRE)) {
        return false;
    }

    BaseType_t higher_priority_task_woken = pdFALSE;
    if (transmit) {
        const size_t expected_bytes =
            STACKCHAN_I2S_TAP_SAMPLES * sizeof(int16_t);
        if (pcm == NULL || bytes != expected_bytes ||
            s_tx_tap_queue == NULL) {
            __atomic_fetch_add(
                &s_tap_tx_drops, 1, __ATOMIC_RELAXED);
            return false;
        }
        s_tx_isr_chunk.sequence = sequence;
        s_tx_isr_chunk.sample_count = STACKCHAN_I2S_TAP_SAMPLES;
        memcpy(s_tx_isr_chunk.pcm, pcm, expected_bytes);
        if (xQueueSendFromISR(
                s_tx_tap_queue, &s_tx_isr_chunk,
                &higher_priority_task_woken) != pdTRUE) {
            __atomic_fetch_add(
                &s_tap_tx_drops, 1, __ATOMIC_RELAXED);
        }
    } else {
        const size_t expected_bytes =
            STACKCHAN_I2S_TAP_SAMPLES *
            STACKCHAN_TDM_CAPTURE_CHANNELS * sizeof(int16_t);
        if (pcm == NULL || bytes != expected_bytes ||
            s_rx_tap_queue == NULL) {
            __atomic_fetch_add(
                &s_tap_rx_drops, 1, __ATOMIC_RELAXED);
            return false;
        }
        const int16_t *interleaved = (const int16_t *)pcm;
        s_rx_isr_chunk.sequence = sequence;
        s_rx_isr_chunk.sample_count = STACKCHAN_I2S_TAP_SAMPLES;
        for (size_t index = 0;
             index < STACKCHAN_I2S_TAP_SAMPLES; ++index) {
            const size_t base =
                index * STACKCHAN_TDM_CAPTURE_CHANNELS;
            for (size_t slot = 0;
                 slot < STACKCHAN_TDM_CAPTURE_CHANNELS; ++slot) {
                const int32_t value = interleaved[base + slot];
                const uint32_t magnitude =
                    (uint32_t)(value < 0 ? -value : value);
                const uint32_t previous = __atomic_load_n(
                    &s_tdm_slot_peaks[slot], __ATOMIC_RELAXED);
                if (magnitude > previous) {
                    __atomic_store_n(
                        &s_tdm_slot_peaks[slot], magnitude,
                        __ATOMIC_RELAXED);
                }
            }
            s_rx_isr_chunk.near[index] =
                interleaved[base + STACKCHAN_TDM_NEAR_INDEX];
            s_rx_isr_chunk.hardware_reference[index] =
                interleaved[base + STACKCHAN_TDM_REFERENCE_INDEX];
        }
        if (xQueueSendFromISR(
                s_rx_tap_queue, &s_rx_isr_chunk,
                &higher_priority_task_woken) != pdTRUE) {
            __atomic_fetch_add(
                &s_tap_rx_drops, 1, __ATOMIC_RELAXED);
        }
    }
    return higher_priority_task_woken == pdTRUE;
}

static bool receive_aligned_dma_pair(audio_tx_dma_chunk_t *transmit,
                                     audio_rx_dma_chunk_t *receive)
{
    bool have_transmit = false;
    bool have_receive = false;
    while (true) {
        if (!have_transmit) {
            if (xQueueReceive(
                    s_tx_tap_queue, transmit, portMAX_DELAY) != pdTRUE) {
                return false;
            }
            have_transmit =
                transmit->sample_count == STACKCHAN_I2S_TAP_SAMPLES;
            if (!have_transmit) {
                __atomic_fetch_add(
                    &s_tap_sequence_skips, 1, __ATOMIC_RELAXED);
            }
        }
        if (!have_receive) {
            if (xQueueReceive(
                    s_rx_tap_queue, receive, portMAX_DELAY) != pdTRUE) {
                return false;
            }
            have_receive =
                receive->sample_count == STACKCHAN_I2S_TAP_SAMPLES;
            if (!have_receive) {
                __atomic_fetch_add(
                    &s_tap_sequence_skips, 1, __ATOMIC_RELAXED);
            }
        }
        if (!have_transmit || !have_receive) {
            continue;
        }

        const int32_t sequence_delta =
            (int32_t)(transmit->sequence - receive->sequence);
        if (sequence_delta == 0) {
            __atomic_fetch_add(&s_tap_pairs, 1, __ATOMIC_RELAXED);
            return true;
        }

        __atomic_fetch_add(
            &s_tap_sequence_skips, 1, __ATOMIC_RELAXED);
        if (sequence_delta < 0) {
            have_transmit = false;
        } else {
            have_receive = false;
        }
    }
}

/*
 * Keep both blocking codec queues serviced, but do no DSP here. The
 * authoritative AEC inputs come from the completed DMA buffers observed by
 * audio_i2s_tap(), not from when these blocking calls happen to return.
 */
static void audio_task(void *argument)
{
    (void)argument;
    const size_t frame_bytes = s_frame_samples * sizeof(int16_t);
    const size_t capture_frame_bytes =
        frame_bytes * STACKCHAN_TDM_CAPTURE_CHANNELS;

    /*
     * RX starts when the codec is opened, before this task is scheduled. Drain
     * the initial DMA backlog so the first processed mic frame is current
     * rather than permanently trailing playback by the whole RX queue.
     */
    for (int frame = 0; frame < STACKCHAN_AUDIO_RX_PRIME_FRAMES; ++frame) {
        if (esp_codec_dev_read(
                s_microphone, s_drain_frame,
                (int)capture_frame_bytes) != ESP_OK) {
            app_status_note_audio_read_error();
            break;
        }
    }
    ESP_LOGI(TAG, "Discarded %d startup RX frame(s) to align the duplex clock",
             STACKCHAN_AUDIO_RX_PRIME_FRAMES);

    ESP_LOGI(TAG, "Audio clock running: %u Hz, %u samples/frame",
             STACKCHAN_AUDIO_SAMPLE_RATE, (unsigned)s_frame_samples);
    audio_pipeline_timing_reset();

    while (true) {
        if (diagnostics_is_running()) {
            diagnostics_fill_playback(s_playback_frame, s_frame_samples);
        } else {
            memset(s_playback_frame, 0, frame_bytes);
            const size_t received =
                xStreamBufferReceive(
                    s_playback, s_playback_frame, frame_bytes, 0);
            if (received > 0 && received < frame_bytes) {
                memset((uint8_t *)s_playback_frame + received, 0,
                       frame_bytes - received);
                app_status_note_playback_underrun();
            }
        }
        level_playback(s_playback_frame, s_frame_samples);

        const int64_t write_started_us = esp_timer_get_time();
        if (esp_codec_dev_write(
                s_speaker, s_playback_frame,
                (int)frame_bytes) != ESP_OK) {
            app_status_note_audio_write_error();
            memset(s_playback_frame, 0, frame_bytes);
        }
        const int64_t write_finished_us = esp_timer_get_time();
        if (esp_codec_dev_read(
                s_microphone, s_drain_frame,
                (int)capture_frame_bytes) != ESP_OK) {
            app_status_note_audio_read_error();
        }
        const int64_t read_finished_us = esp_timer_get_time();
        __atomic_store_n(
            &s_latest_write_us,
            (uint32_t)(write_finished_us - write_started_us),
            __ATOMIC_RELAXED);
        __atomic_store_n(
            &s_latest_read_us,
            (uint32_t)(read_finished_us - write_finished_us),
            __ATOMIC_RELAXED);
    }
}

static void aec_task(void *argument)
{
    (void)argument;
    const size_t frame_bytes = s_frame_samples * sizeof(int16_t);
    audio_tx_dma_chunk_t transmit;
    audio_rx_dma_chunk_t receive;
    uint32_t peak_log_frames = 0;

    while (true) {
        const uint32_t timing_epoch =
            __atomic_load_n(&s_timing_epoch, __ATOMIC_RELAXED);
        const int64_t frame_started_us = esp_timer_get_time();
        for (size_t offset = 0; offset < s_frame_samples;
             offset += STACKCHAN_I2S_TAP_SAMPLES) {
            if (!receive_aligned_dma_pair(&transmit, &receive)) {
                continue;
            }
            memcpy(
                s_reference_frame + offset, transmit.pcm,
                STACKCHAN_I2S_TAP_SAMPLES * sizeof(int16_t));
            memcpy(
                s_raw_frame + offset, receive.near,
                STACKCHAN_I2S_TAP_SAMPLES * sizeof(int16_t));
            memcpy(
                s_hardware_reference_frame + offset,
                receive.hardware_reference,
                STACKCHAN_I2S_TAP_SAMPLES * sizeof(int16_t));
            /*
             * This PCM has actually completed DMA playout, so the face cannot
             * lead the speaker because of network or software queue depth.
             */
            face_animator_push_pcm(
                &s_face_animator, transmit.pcm,
                STACKCHAN_I2S_TAP_SAMPLES);
        }

        const int64_t reference_started_us = esp_timer_get_time();
        prepare_aec_reference(
            s_hardware_reference_frame, s_frame_samples);
        const int64_t reference_finished_us = esp_timer_get_time();
        const int requested_nlp_level =
            __atomic_load_n(&s_aec_nlp_level, __ATOMIC_RELAXED);
        if (requested_nlp_level != s_active_aec_nlp_level) {
            s_active_aec_nlp_level = (int)aec_set_nlp_level(
                s_aec, (aec_nlp_level_t)requested_nlp_level);
            ESP_LOGI(TAG, "AEC nonlinear suppression level applied: %d",
                     s_active_aec_nlp_level);
        }
        const int64_t aec_started_us = esp_timer_get_time();
        aec_linear_process(
            s_aec, s_raw_frame, s_aec_reference_frame, s_clean_frame);
        const int64_t aec_linear_finished_us = esp_timer_get_time();
        (void)aec_nlp_process(s_aec, s_clean_frame);
        const int64_t aec_finished_us = esp_timer_get_time();
        diagnostics_record_frame(
            s_raw_frame, s_hardware_reference_frame, s_clean_frame,
            s_frame_samples);
        audio_trace_record(
            s_raw_frame, s_hardware_reference_frame, s_clean_frame,
            s_frame_samples);

        if (xStreamBufferSpacesAvailable(s_clean) >= frame_bytes) {
            (void)xStreamBufferSend(
                s_clean, s_clean_frame, frame_bytes, 0);
        }
        const int64_t frame_finished_us = esp_timer_get_time();
        record_audio_timing(
            timing_epoch,
            (uint32_t)(frame_finished_us - frame_started_us),
            __atomic_load_n(&s_latest_write_us, __ATOMIC_RELAXED),
            __atomic_load_n(&s_latest_read_us, __ATOMIC_RELAXED),
            (uint32_t)(reference_finished_us - reference_started_us),
            (uint32_t)(aec_finished_us - aec_started_us),
            (uint32_t)(aec_linear_finished_us - aec_started_us),
            (uint32_t)(aec_finished_us - aec_linear_finished_us));
        app_status_note_audio_frame();
        peak_log_frames++;
        if (peak_log_frames >=
            STACKCHAN_AUDIO_SAMPLE_RATE / s_frame_samples) {
            peak_log_frames = 0;
            const uint32_t slot0 = __atomic_exchange_n(
                &s_tdm_slot_peaks[0], 0, __ATOMIC_RELAXED);
            const uint32_t slot1 = __atomic_exchange_n(
                &s_tdm_slot_peaks[1], 0, __ATOMIC_RELAXED);
            const uint32_t slot2 = __atomic_exchange_n(
                &s_tdm_slot_peaks[2], 0, __ATOMIC_RELAXED);
            const uint32_t slot3 = __atomic_exchange_n(
                &s_tdm_slot_peaks[3], 0, __ATOMIC_RELAXED);
            ESP_LOGI(
                TAG, "ES7210 TDM one-second peaks: [%u, %u, %u, %u]",
                (unsigned)slot0, (unsigned)slot1,
                (unsigned)slot2, (unsigned)slot3);
        }
    }
}

esp_err_t audio_pipeline_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    face_animator_init(&s_face_animator, STACKCHAN_AUDIO_SAMPLE_RATE);
    __atomic_store_n(&s_face_ready, true, __ATOMIC_RELEASE);

    s_tx_tap_queue = xQueueCreateStatic(
        STACKCHAN_I2S_TAP_QUEUE_DEPTH, sizeof(audio_tx_dma_chunk_t),
        s_tx_tap_queue_storage, &s_tx_tap_queue_control);
    s_rx_tap_queue = xQueueCreateStatic(
        STACKCHAN_I2S_TAP_QUEUE_DEPTH, sizeof(audio_rx_dma_chunk_t),
        s_rx_tap_queue_storage, &s_rx_tap_queue_control);
    if (s_tx_tap_queue == NULL || s_rx_tap_queue == NULL) {
        ESP_LOGE(TAG, "Unable to create I2S DMA tap queues");
        return ESP_ERR_NO_MEM;
    }
    bsp_audio_i2s_set_tap(audio_i2s_tap, NULL);

    log_heap("Before audio initialization");
    ESP_RETURN_ON_ERROR(initialise_codecs(), TAG, "Initialize codecs");
    log_heap("After codec initialization");

    aec_config_t config = {
        .mic_num = 1,
        .ref_num = 1,
        .out_num = 1,
        .filter_length = STACKCHAN_AEC_FILTER_LENGTH,
        .sample_rate = STACKCHAN_AUDIO_SAMPLE_RATE,
        .caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        .mode = STACKCHAN_AEC_MODE,
        .nlp_level = (aec_nlp_level_t)STACKCHAN_AEC_NLP_LEVEL,
    };
    s_aec = aec_create_from_config(&config);
    if (s_aec == NULL) {
        ESP_LOGE(TAG, "ESP-SR full-duplex AEC creation failed");
        return ESP_FAIL;
    }
    s_frame_samples = (size_t)aec_get_chunksize(s_aec);
    if (s_frame_samples == 0 ||
        s_frame_samples % STACKCHAN_I2S_TAP_SAMPLES != 0) {
        ESP_LOGE(
            TAG, "AEC frame size %u is incompatible with %u-sample DMA taps",
            (unsigned)s_frame_samples,
            (unsigned)STACKCHAN_I2S_TAP_SAMPLES);
        return ESP_FAIL;
    }
    log_heap("After AEC creation");

    ESP_RETURN_ON_ERROR(
        audio_trace_init(), TAG, "Allocate rolling audio trace");
    s_playback_storage = heap_caps_malloc(
        PLAYBACK_STORAGE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_clean_storage = heap_caps_malloc(
        CLEAN_STORAGE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_playback_storage == NULL || s_clean_storage == NULL) {
        ESP_LOGE(TAG, "Unable to allocate PSRAM stream storage");
        return ESP_ERR_NO_MEM;
    }
    s_playback = xStreamBufferCreateStatic(
        PLAYBACK_STORAGE_BYTES, 1, s_playback_storage,
        &s_playback_control);
    s_clean = xStreamBufferCreateStatic(
        CLEAN_STORAGE_BYTES, 1, s_clean_storage, &s_clean_control);
    if (s_playback == NULL || s_clean == NULL) {
        ESP_LOGE(TAG, "Unable to create static audio stream buffers");
        return ESP_ERR_NO_MEM;
    }

    s_raw_frame = aligned_audio_buffer(s_frame_samples);
    s_reference_frame = aligned_audio_buffer(s_frame_samples);
    s_hardware_reference_frame = aligned_audio_buffer(s_frame_samples);
    s_playback_frame = aligned_audio_buffer(s_frame_samples);
    s_drain_frame = aligned_audio_buffer(
        s_frame_samples * STACKCHAN_TDM_CAPTURE_CHANNELS);
    s_aec_reference_frame = aligned_audio_buffer(s_frame_samples);
    s_clean_frame = aligned_audio_buffer(s_frame_samples);
    const size_t maximum_reference_delay_samples =
        STACKCHAN_AEC_REFERENCE_DELAY_MAX_MS *
        STACKCHAN_AUDIO_SAMPLE_RATE / 1000;
    s_reference_delay_storage = heap_caps_calloc(
        maximum_reference_delay_samples, sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_raw_frame == NULL || s_reference_frame == NULL ||
        s_hardware_reference_frame == NULL ||
        s_playback_frame == NULL || s_drain_frame == NULL ||
        s_aec_reference_frame == NULL || s_clean_frame == NULL ||
        s_reference_delay_storage == NULL) {
        ESP_LOGE(TAG, "Unable to allocate AEC frame or delay storage");
        return ESP_ERR_NO_MEM;
    }
    log_heap("Before audio task creation");
    BaseType_t created = xTaskCreatePinnedToCoreWithCaps(
        aec_task, "aec_process", STACKCHAN_AEC_TASK_STACK, NULL,
        STACKCHAN_AEC_TASK_PRIORITY, &s_aec_task,
        STACKCHAN_AEC_TASK_CORE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create AEC processing task");
        return ESP_ERR_NO_MEM;
    }
    created = xTaskCreatePinnedToCore(
        audio_task, "aec_audio", STACKCHAN_AUDIO_IO_TASK_STACK, NULL,
        STACKCHAN_AUDIO_IO_TASK_PRIORITY, &s_audio_task,
        STACKCHAN_AEC_TASK_CORE);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create audio I/O task");
        return ESP_ERR_NO_MEM;
    }
    xQueueReset(s_tx_tap_queue);
    xQueueReset(s_rx_tap_queue);
    __atomic_store_n(&s_tap_enabled, true, __ATOMIC_RELEASE);
    s_ready = true;
    log_heap("Audio initialization complete");
    return ESP_OK;
}

bool audio_pipeline_is_ready(void)
{
    return s_ready;
}

void audio_pipeline_face_snapshot(face_animator_state_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    if (!__atomic_load_n(&s_face_ready, __ATOMIC_ACQUIRE)) {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->eye_open = UINT8_MAX;
        return;
    }
    face_animator_snapshot(&s_face_animator, snapshot);
}

void audio_pipeline_face_event(const face_stream_event_t *event)
{
    if (!__atomic_load_n(&s_face_ready, __ATOMIC_ACQUIRE)) {
        return;
    }
    face_animator_push_event(&s_face_animator, event);
}

esp_err_t audio_pipeline_set_speaker_volume(int volume_percent)
{
    if (volume_percent < 0 || volume_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_speaker == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result =
        esp_codec_dev_set_out_vol(s_speaker, volume_percent);
    if (result == ESP_OK) {
        s_speaker_volume = volume_percent;
        ESP_LOGI(TAG, "Speaker volume set to %d%%", volume_percent);
    }
    return result;
}

int audio_pipeline_speaker_volume(void)
{
    return s_speaker_volume;
}

esp_err_t audio_pipeline_set_microphone_gain(int gain_db)
{
    if (gain_db < 0 || gain_db > 37) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_microphone == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result =
        esp_codec_dev_set_in_channel_gain(
            s_microphone, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
            (float)gain_db);
    if (result == ESP_OK) {
        s_microphone_gain_db = gain_db;
        audio_pipeline_timing_reset();
        ESP_LOGI(TAG, "Microphone analog gain set to %d dB", gain_db);
    }
    return result;
}

int audio_pipeline_microphone_gain(void)
{
    return s_microphone_gain_db;
}

esp_err_t audio_pipeline_set_aec_reference_delay_ms(int delay_ms)
{
    if (delay_ms < 0 ||
        delay_ms > STACKCHAN_AEC_REFERENCE_DELAY_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    __atomic_store_n(
        &s_aec_reference_delay_ms, delay_ms, __ATOMIC_RELAXED);
    audio_pipeline_timing_reset();
    ESP_LOGI(
        TAG,
        "AEC reference offset set to %d ms (speaker playback remains live)",
        delay_ms);
    return ESP_OK;
}

int audio_pipeline_aec_reference_delay_ms(void)
{
    return __atomic_load_n(
        &s_aec_reference_delay_ms, __ATOMIC_RELAXED);
}

esp_err_t audio_pipeline_set_aec_nlp_level(int level)
{
    if (level < AEC_NLP_LEVEL_NORMAL ||
        level > AEC_NLP_LEVEL_VERYAGGR) {
        return ESP_ERR_INVALID_ARG;
    }
    __atomic_store_n(&s_aec_nlp_level, level, __ATOMIC_RELAXED);
    audio_pipeline_timing_reset();
    ESP_LOGI(TAG, "AEC nonlinear suppression level requested: %d", level);
    return ESP_OK;
}

void audio_pipeline_timing_snapshot(
    audio_pipeline_timing_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->epoch =
        __atomic_load_n(&s_timing_epoch, __ATOMIC_RELAXED);
    snapshot->frames =
        __atomic_load_n(&s_timing_frames, __ATOMIC_RELAXED);
    snapshot->over_budget_frames =
        __atomic_load_n(
            &s_timing_over_budget_frames, __ATOMIC_RELAXED);
    snapshot->last_frame_us =
        __atomic_load_n(&s_timing_last_frame_us, __ATOMIC_RELAXED);
    snapshot->maximum_frame_us =
        __atomic_load_n(&s_timing_maximum_frame_us, __ATOMIC_RELAXED);
    snapshot->last_write_us =
        __atomic_load_n(&s_timing_last_write_us, __ATOMIC_RELAXED);
    snapshot->maximum_write_us =
        __atomic_load_n(&s_timing_maximum_write_us, __ATOMIC_RELAXED);
    snapshot->last_read_us =
        __atomic_load_n(&s_timing_last_read_us, __ATOMIC_RELAXED);
    snapshot->maximum_read_us =
        __atomic_load_n(&s_timing_maximum_read_us, __ATOMIC_RELAXED);
    snapshot->last_reference_us =
        __atomic_load_n(&s_timing_last_reference_us, __ATOMIC_RELAXED);
    snapshot->maximum_reference_us =
        __atomic_load_n(
            &s_timing_maximum_reference_us, __ATOMIC_RELAXED);
    snapshot->last_aec_us =
        __atomic_load_n(&s_timing_last_aec_us, __ATOMIC_RELAXED);
    snapshot->maximum_aec_us =
        __atomic_load_n(&s_timing_maximum_aec_us, __ATOMIC_RELAXED);
    snapshot->last_aec_linear_us =
        __atomic_load_n(
            &s_timing_last_aec_linear_us, __ATOMIC_RELAXED);
    snapshot->maximum_aec_linear_us =
        __atomic_load_n(
            &s_timing_maximum_aec_linear_us, __ATOMIC_RELAXED);
    snapshot->last_aec_nlp_us =
        __atomic_load_n(&s_timing_last_aec_nlp_us, __ATOMIC_RELAXED);
    snapshot->maximum_aec_nlp_us =
        __atomic_load_n(
            &s_timing_maximum_aec_nlp_us, __ATOMIC_RELAXED);
    const uint32_t frames = snapshot->frames;
    if (frames > 0) {
        snapshot->average_frame_us =
            __atomic_load_n(
                &s_timing_total_frame_us, __ATOMIC_RELAXED) / frames;
        snapshot->average_write_us =
            __atomic_load_n(
                &s_timing_total_write_us, __ATOMIC_RELAXED) / frames;
        snapshot->average_read_us =
            __atomic_load_n(
                &s_timing_total_read_us, __ATOMIC_RELAXED) / frames;
        snapshot->average_reference_us =
            __atomic_load_n(
                &s_timing_total_reference_us, __ATOMIC_RELAXED) / frames;
        snapshot->average_aec_us =
            __atomic_load_n(
                &s_timing_total_aec_us, __ATOMIC_RELAXED) / frames;
        snapshot->average_aec_linear_us =
            __atomic_load_n(
                &s_timing_total_aec_linear_us, __ATOMIC_RELAXED) / frames;
        snapshot->average_aec_nlp_us =
            __atomic_load_n(
                &s_timing_total_aec_nlp_us, __ATOMIC_RELAXED) / frames;
    }

    bsp_audio_i2s_stats_t i2s = {0};
    bsp_audio_i2s_stats_snapshot(&i2s);
    snapshot->tx_dma_events = i2s.tx_dma_events;
    snapshot->tx_queue_overflows = i2s.tx_queue_overflows;
    snapshot->rx_dma_events = i2s.rx_dma_events;
    snapshot->rx_queue_overflows = i2s.rx_queue_overflows;
    snapshot->dma_tap_pairs =
        __atomic_load_n(&s_tap_pairs, __ATOMIC_RELAXED);
    snapshot->dma_tap_tx_drops =
        __atomic_load_n(&s_tap_tx_drops, __ATOMIC_RELAXED);
    snapshot->dma_tap_rx_drops =
        __atomic_load_n(&s_tap_rx_drops, __ATOMIC_RELAXED);
    snapshot->dma_tap_sequence_skips =
        __atomic_load_n(&s_tap_sequence_skips, __ATOMIC_RELAXED);
    if (s_audio_task != NULL) {
        snapshot->audio_stack_minimum_free_bytes =
            (uint32_t)uxTaskGetStackHighWaterMark(s_audio_task);
    }
    if (s_aec_task != NULL) {
        const uint32_t aec_free =
            (uint32_t)uxTaskGetStackHighWaterMark(s_aec_task);
        if (snapshot->audio_stack_minimum_free_bytes == 0 ||
            aec_free < snapshot->audio_stack_minimum_free_bytes) {
            snapshot->audio_stack_minimum_free_bytes = aec_free;
        }
    }
}

void audio_pipeline_timing_reset(void)
{
    __atomic_fetch_add(&s_timing_epoch, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_frames, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_over_budget_frames, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_last_frame_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_maximum_frame_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_last_write_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_maximum_write_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_last_read_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_maximum_read_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_last_reference_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_maximum_reference_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_last_aec_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_maximum_aec_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_last_aec_linear_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_maximum_aec_linear_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_last_aec_nlp_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_maximum_aec_nlp_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_total_frame_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_total_write_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_total_read_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_total_reference_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_timing_total_aec_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_total_aec_linear_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_timing_total_aec_nlp_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tap_pairs, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tap_tx_drops, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tap_rx_drops, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &s_tap_sequence_skips, 0, __ATOMIC_RELAXED);
    bsp_audio_i2s_stats_reset();
}

int audio_pipeline_aec_nlp_level(void)
{
    return __atomic_load_n(&s_aec_nlp_level, __ATOMIC_RELAXED);
}

const char *audio_pipeline_aec_mode_name(void)
{
    return STACKCHAN_AEC_MODE_NAME;
}

esp_err_t audio_pipeline_set_playback_gain(int gain_db)
{
    if (!speech_leveler_gain_is_valid(gain_db)) {
        return ESP_ERR_INVALID_ARG;
    }
    __atomic_store_n(&s_playback_gain_db, gain_db, __ATOMIC_RELAXED);
    ESP_LOGI(TAG, "Playback speech gain set to +%d dB", gain_db);
    return ESP_OK;
}

void audio_pipeline_level_snapshot(audio_pipeline_level_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    snapshot->gain_db =
        __atomic_load_n(&s_playback_gain_db, __ATOMIC_RELAXED);
    snapshot->processed_samples =
        __atomic_load_n(&s_level_processed_samples, __ATOMIC_RELAXED);
    snapshot->limited_samples =
        __atomic_load_n(&s_level_limited_samples, __ATOMIC_RELAXED);
    snapshot->overrange_samples =
        __atomic_load_n(&s_level_overrange_samples, __ATOMIC_RELAXED);
    snapshot->source_peak =
        __atomic_load_n(&s_level_source_peak, __ATOMIC_RELAXED);
    snapshot->pre_limiter_peak =
        __atomic_load_n(&s_level_pre_limiter_peak, __ATOMIC_RELAXED);
    snapshot->output_peak =
        __atomic_load_n(&s_level_output_peak, __ATOMIC_RELAXED);
}

size_t audio_pipeline_write_playback(const int16_t *samples,
                                     size_t sample_count,
                                     TickType_t timeout)
{
    if (!s_ready || s_playback == NULL || samples == NULL ||
        sample_count == 0 ||
        diagnostics_is_running()) {
        return 0;
    }
    const size_t bytes = xStreamBufferSend(
        s_playback, samples, sample_count * sizeof(int16_t), timeout);
    return bytes / sizeof(int16_t);
}

size_t audio_pipeline_read_clean(int16_t *samples, size_t sample_capacity,
                                 TickType_t timeout)
{
    if (!s_ready || s_clean == NULL || samples == NULL ||
        sample_capacity == 0) {
        return 0;
    }
    const size_t bytes = xStreamBufferReceive(
        s_clean, samples, sample_capacity * sizeof(int16_t), timeout);
    return bytes / sizeof(int16_t);
}

void audio_pipeline_flush_playback(void)
{
    if (s_playback != NULL) {
        (void)xStreamBufferReset(s_playback);
    }
}

void audio_pipeline_flush_clean(void)
{
    if (s_clean != NULL) {
        (void)xStreamBufferReset(s_clean);
    }
}

size_t audio_pipeline_playback_pending_samples(void)
{
    if (s_playback == NULL) {
        return 0;
    }
    return xStreamBufferBytesAvailable(s_playback) / sizeof(int16_t);
}

size_t audio_pipeline_clean_pending_samples(void)
{
    if (s_clean == NULL) {
        return 0;
    }
    return xStreamBufferBytesAvailable(s_clean) / sizeof(int16_t);
}

size_t audio_pipeline_frame_samples(void)
{
    return s_frame_samples;
}
