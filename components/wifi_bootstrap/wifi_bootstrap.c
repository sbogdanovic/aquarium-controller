#include "wifi_bootstrap.h"
#include "wifi_bootstrap_config.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "wifi_bootstrap";
#if CONFIG_WIFI_BOOTSTRAP_ENABLE
static EventGroupHandle_t s_wifi_events;
#endif

#if CONFIG_WIFI_BOOTSTRAP_ENABLE
static esp_err_t ensure_nvs_ready(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requires erase, reinitializing");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs_flash_erase failed");
        err = nvs_flash_init();
    }

    return err;
}

static void
wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Station start: attempting initial connect");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected =
            (const wifi_event_sta_disconnected_t *)event_data;

        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(
            TAG, "Disconnected from AP (reason=%d), scheduling reconnect", disconnected->reason);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;

        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected, IP=" IPSTR, IP2STR(&event->ip_info.ip));
    }
}
#endif

esp_err_t wifi_bootstrap_start(void)
{
#if !CONFIG_WIFI_BOOTSTRAP_ENABLE
    ESP_LOGI(TAG, "Wi-Fi bootstrap disabled by config");
    return ESP_OK;
#else
    const wifi_bootstrap_config_status_t cfg_status =
        wifi_bootstrap_validate_config(CONFIG_WIFI_BOOTSTRAP_SSID, CONFIG_WIFI_BOOTSTRAP_PASSWORD);

    if (!wifi_bootstrap_should_attempt_connect(cfg_status)) {
        ESP_LOGW(TAG, "Disabled-Misconfigured Wi-Fi: SSID is empty; skipping connection attempts");
        return ESP_OK;
    }

    if (cfg_status == WIFI_BOOTSTRAP_CONFIG_STATUS_OPEN_NETWORK) {
        ESP_LOGW(TAG, "Wi-Fi password is empty; proceeding as open network");
    }

    esp_err_t err = ensure_nvs_ready();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (s_wifi_events == NULL) {
        s_wifi_events = xEventGroupCreate();
    }

    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL),
        TAG,
        "register WIFI_EVENT handler failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL),
        TAG,
        "register IP_EVENT handler failed");

    wifi_config_t wifi_cfg = {
        .sta =
            {
                .threshold.authmode = WIFI_AUTH_OPEN,
                .pmf_cfg =
                    {
                        .capable = true,
                        .required = false,
                    },
            },
    };

    strlcpy((char *)wifi_cfg.sta.ssid, CONFIG_WIFI_BOOTSTRAP_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password,
            CONFIG_WIFI_BOOTSTRAP_PASSWORD,
            sizeof(wifi_cfg.sta.password));

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "set config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    if (CONFIG_WIFI_BOOTSTRAP_STARTUP_TIMEOUT_MS == 0) {
        ESP_LOGW(TAG,
                 "Startup connect window is 0ms; continuing immediately in Network Degraded until "
                 "connected");
        return ESP_OK;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdTRUE,
                                           pdMS_TO_TICKS(CONFIG_WIFI_BOOTSTRAP_STARTUP_TIMEOUT_MS));

    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGW(TAG,
                 "Startup connect window (%dms) elapsed; continuing in Network Degraded",
                 CONFIG_WIFI_BOOTSTRAP_STARTUP_TIMEOUT_MS);
    }

    return ESP_OK;
#endif
}
