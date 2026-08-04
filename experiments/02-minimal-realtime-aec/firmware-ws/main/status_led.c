#include "status_led.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp/m5stack_core_s3.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_private/i2c_platform.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * Register protocol ported directly from M5Stack's MIT-licensed
 * StackChan-BSP PY32IOExpander driver. The body is on the CoreS3 internal I2C
 * bus (GPIO 11/12) at 0x6f and owns 12 RGB565 LEDs.
 */
#define STACKCHAN_BODY_ADDRESS 0x6f
#define STACKCHAN_BODY_LED_COUNT 12U
#define PY32_REG_VERSION 0x02
#define PY32_REG_GPIO_MODE_H 0x04
#define PY32_REG_GPIO_PULL_UP_H 0x0a
#define PY32_REG_GPIO_PULL_DOWN_H 0x0c
#define PY32_REG_GPIO_DRIVE_H 0x14
#define PY32_REG_LED_CFG 0x24
#define PY32_REG_LED_RAM_START 0x30
#define PY32_LED_PIN_H_BIT (1U << (13U - 8U))
#define PY32_LED_REFRESH_BIT (1U << 6U)

static const char *TAG = "status_led";
static i2c_master_dev_handle_t s_device;
static status_led_state_t s_state = STATUS_LED_OFF;
static bool s_ready;

static esp_err_t transmit_with_retry(
    const uint8_t *write_buffer,
    size_t write_size,
    uint8_t *read_buffer,
    size_t read_size)
{
    esp_err_t result = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 4U; attempt++) {
        if (read_size > 0U) {
            result = i2c_master_transmit_receive(
                s_device,
                write_buffer,
                write_size,
                read_buffer,
                read_size,
                pdMS_TO_TICKS(100));
        } else {
            result = i2c_master_transmit(
                s_device,
                write_buffer,
                write_size,
                pdMS_TO_TICKS(100));
        }
        if (result == ESP_OK) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return result;
}

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    return transmit_with_retry(
        &reg, sizeof(reg), value, sizeof(*value));
}

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    const uint8_t command[] = {reg, value};
    return transmit_with_retry(
        command, sizeof(command), NULL, 0U);
}

static esp_err_t update_register(
    uint8_t reg, uint8_t set_bits, uint8_t clear_bits)
{
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(read_register(reg, &value), TAG, "Read reg 0x%02x", reg);
    value = (uint8_t)((value | set_bits) & (uint8_t)~clear_bits);
    return write_register(reg, value);
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(
        ((uint16_t)(red & 0xf8U) << 8U) |
        ((uint16_t)(green & 0xfcU) << 3U) |
        ((uint16_t)blue >> 3U));
}

static esp_err_t show_colour(uint8_t red, uint8_t green, uint8_t blue)
{
    uint8_t command[1U + STACKCHAN_BODY_LED_COUNT * 2U];
    command[0] = PY32_REG_LED_RAM_START;
    const uint16_t colour = rgb565(red, green, blue);
    for (size_t index = 0; index < STACKCHAN_BODY_LED_COUNT; index++) {
        command[1U + index * 2U] = (uint8_t)(colour & 0xffU);
        command[2U + index * 2U] = (uint8_t)(colour >> 8U);
    }
    ESP_RETURN_ON_ERROR(
        transmit_with_retry(command, sizeof(command), NULL, 0U),
        TAG, "Write LED pixels");
    return update_register(
        PY32_REG_LED_CFG, PY32_LED_REFRESH_BIT, 0U);
}

esp_err_t status_led_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(
        bsp_i2c_init(), TAG, "Initialize shared CoreS3 I2C");

    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(
        i2c_master_get_bus_handle(BSP_I2C_NUM, &bus),
        TAG, "Get shared CoreS3 I2C bus");

    /*
     * The official Arduino BSP allows 1.2 seconds for the body MCU to boot.
     * Probe without making StackChan's voice startup depend on the body.
     */
    esp_err_t probe_result = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 6U; attempt++) {
        probe_result = i2c_master_probe(
            bus, STACKCHAN_BODY_ADDRESS, pdMS_TO_TICKS(100));
        if (probe_result == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (probe_result != ESP_OK) {
        ESP_LOGW(TAG, "StackChan body LEDs not detected at I2C 0x6f");
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = STACKCHAN_BODY_ADDRESS,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(bus, &config, &s_device),
        TAG, "Attach StackChan body I2C device");

    uint8_t version = 0;
    ESP_RETURN_ON_ERROR(
        read_register(PY32_REG_VERSION, &version),
        TAG, "Read StackChan body version");
    if (version == 0U || version == 0xffU) {
        ESP_LOGW(TAG, "Invalid StackChan body version 0x%02x", version);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_RETURN_ON_ERROR(
        update_register(
            PY32_REG_GPIO_MODE_H, PY32_LED_PIN_H_BIT, 0U),
        TAG, "Set LED pin output");
    ESP_RETURN_ON_ERROR(
        update_register(
            PY32_REG_GPIO_PULL_DOWN_H, 0U, PY32_LED_PIN_H_BIT),
        TAG, "Disable LED pin pull-down");
    ESP_RETURN_ON_ERROR(
        update_register(
            PY32_REG_GPIO_PULL_UP_H, PY32_LED_PIN_H_BIT, 0U),
        TAG, "Enable LED pin pull-up");
    ESP_RETURN_ON_ERROR(
        update_register(
            PY32_REG_GPIO_DRIVE_H, 0U, PY32_LED_PIN_H_BIT),
        TAG, "Set LED pin push-pull");
    ESP_RETURN_ON_ERROR(
        write_register(PY32_REG_LED_CFG, STACKCHAN_BODY_LED_COUNT),
        TAG, "Configure 12 body LEDs");

    s_state = STATUS_LED_OFF;
    ESP_RETURN_ON_ERROR(show_colour(0U, 0U, 0U), TAG, "Clear body LEDs");
    s_ready = true;
    ESP_LOGI(TAG, "StackChan body LEDs ready: version=0x%02x count=12", version);
    return ESP_OK;
}

void status_led_set(status_led_state_t state)
{
    if (!s_ready || state == s_state) {
        return;
    }

    esp_err_t result = ESP_OK;
    switch (state) {
    case STATUS_LED_LISTENING:
        /* Deliberately restrained: readable without illuminating the room. */
        result = show_colour(0U, 48U, 8U);
        break;
    case STATUS_LED_ERROR:
        result = show_colour(64U, 0U, 0U);
        break;
    case STATUS_LED_OFF:
    default:
        result = show_colour(0U, 0U, 0U);
        break;
    }
    if (result == ESP_OK) {
        s_state = state;
    } else {
        ESP_LOGW(TAG, "Unable to update body LEDs: %s", esp_err_to_name(result));
    }
}
