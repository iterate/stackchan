#include "wifi_station.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_MAX_RETRIES 12

static const char *TAG = "wifi";

static EventGroupHandle_t s_events;
static int s_retries;
static char s_ip_address[16];

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)argument;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < WIFI_MAX_RETRIES) {
            s_retries++;
            ESP_LOGW(TAG, "Connection lost; retry %d/%d", s_retries, WIFI_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, WIFI_FAILED_BIT);
        }
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        snprintf(s_ip_address, sizeof(s_ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        s_retries = 0;
        ESP_LOGI(TAG, "Connected at %s", s_ip_address);
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t initialise_nvs(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Erase NVS");
        error = nvs_flash_init();
    }
    return error;
}

esp_err_t wifi_station_connect(uint32_t timeout_ms, char *ip_address,
                               size_t ip_address_size)
{
    if (STACKCHAN_WIFI_SSID[0] == '\0' || STACKCHAN_WIFI_PASSWORD[0] == '\0') {
        ESP_LOGE(TAG, "Local Wi-Fi credentials are not configured");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(initialise_nvs(), TAG, "Initialize NVS");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Initialize network interfaces");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Create event loop");

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_t *station = esp_netif_create_default_wifi_sta();
    if (station == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_hostname(station, STACKCHAN_HOSTNAME));

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Initialize Wi-Fi");

    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t ip_handler;
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &wifi_handler),
        TAG, "Register Wi-Fi handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &ip_handler),
        TAG, "Register IP handler");

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, STACKCHAN_WIFI_SSID, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, STACKCHAN_WIFI_PASSWORD,
            sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Set station mode");
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "Set station config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Start Wi-Fi");

    EventBits_t bits = xEventGroupWaitBits(
        s_events,
        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGE(TAG, "Failed to connect before timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (ip_address != NULL && ip_address_size > 0) {
        strlcpy(ip_address, s_ip_address, ip_address_size);
    }

    esp_err_t mdns_error = mdns_init();
    if (mdns_error == ESP_OK) {
        mdns_hostname_set(STACKCHAN_HOSTNAME);
        mdns_instance_name_set("StackChan Realtime");
        mdns_service_add("StackChan control", "_http", "_tcp",
                         STACKCHAN_HTTP_PORT, NULL, 0);
        ESP_LOGI(TAG, "mDNS ready at http://%s.local", STACKCHAN_HOSTNAME);
    } else {
        ESP_LOGW(TAG, "mDNS unavailable: %s", esp_err_to_name(mdns_error));
    }

    return ESP_OK;
}
