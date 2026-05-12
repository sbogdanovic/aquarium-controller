#include "doser.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define DOSER_TASK_STACK    2048
#define DOSER_TASK_PRIORITY 5
#define DOSER_QUEUE_LEN     4

typedef struct {
    gpio_num_t relay_gpio;
    QueueHandle_t queue;
    TaskHandle_t task_handle;
    char name[12];
} doser_channel_t;

typedef struct {
    doser_config_t cfg;
    doser_channel_t channels[CONFIG_DOSER_COUNT];
} doser_ctx_t;

static const char *TAG = "doser";
static doser_ctx_t s_ctx;
static bool s_started;

static esp_err_t init_channel(doser_channel_t *channel, gpio_num_t gpio, size_t index);
static esp_err_t configure_output_gpio(gpio_num_t gpio);
static void doser_task(void *arg);
static void handle_dose_request(doser_channel_t *channel, uint32_t milliliters);

esp_err_t doser_start(const doser_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.cfg = *config;
    if (s_ctx.cfg.ms_per_ml == 0) {
        s_ctx.cfg.ms_per_ml = CONFIG_DOSER_MS_PER_ML;
    }

    for (size_t i = 0; i < CONFIG_DOSER_COUNT; ++i) {
        esp_err_t ch_err = init_channel(&s_ctx.channels[i], s_ctx.cfg.relay_gpios[i], i);
        if (ch_err != ESP_OK) {
            ESP_LOGE(TAG, "channel %u init failed: %s", (unsigned)i, esp_err_to_name(ch_err));
            for (size_t j = 0; j < i; ++j) {
                if (s_ctx.channels[j].task_handle) {
                    vTaskDelete(s_ctx.channels[j].task_handle);
                    s_ctx.channels[j].task_handle = NULL;
                }
                if (s_ctx.channels[j].queue) {
                    vQueueDelete(s_ctx.channels[j].queue);
                    s_ctx.channels[j].queue = NULL;
                }
            }
            memset(&s_ctx, 0, sizeof(s_ctx));
            return ch_err;
        }
    }

    s_started = true;
    ESP_LOGI(TAG,
             "Doser component started (%d channels, %d ms/ml)",
             CONFIG_DOSER_COUNT,
             (int)s_ctx.cfg.ms_per_ml);
    return ESP_OK;
}

static esp_err_t init_channel(doser_channel_t *channel, gpio_num_t gpio, size_t index)
{
    memset(channel, 0, sizeof(*channel));
    channel->relay_gpio = gpio;
    channel->queue = xQueueCreate(DOSER_QUEUE_LEN, sizeof(uint32_t));
    if (!channel->queue) {
        return ESP_ERR_NO_MEM;
    }

    snprintf(channel->name, sizeof(channel->name), "doser%u", (unsigned)index + 1);

    if (gpio >= 0) {
        ESP_RETURN_ON_ERROR(configure_output_gpio(gpio), TAG, "relay gpio");
    } else {
        ESP_LOGW(TAG, "%s disabled (GPIO%d)", channel->name, (int)gpio);
    }

    if (xTaskCreate(doser_task,
                    channel->name,
                    DOSER_TASK_STACK,
                    channel,
                    DOSER_TASK_PRIORITY,
                    &channel->task_handle) != pdPASS) {
        vQueueDelete(channel->queue);
        channel->queue = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "%s task ready (GPIO%d)", channel->name, (int)gpio);
    return ESP_OK;
}

static esp_err_t configure_output_gpio(gpio_num_t gpio)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(gpio, 0), TAG, "gpio_set_level failed");
    return ESP_OK;
}

static void doser_task(void *arg)
{
    doser_channel_t *channel = (doser_channel_t *)arg;
    uint32_t milliliters = 0;

    while (true) {
        if (xQueueReceive(channel->queue, &milliliters, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        handle_dose_request(channel, milliliters);
    }
}

static void handle_dose_request(doser_channel_t *channel, uint32_t milliliters)
{
    if (!channel || milliliters == 0) {
        return;
    }

    if (channel->relay_gpio < 0) {
        ESP_LOGW(
            TAG, "%s requested %u ml but GPIO is disabled", channel->name, (unsigned)milliliters);
        return;
    }

    const uint64_t duration_ms = (uint64_t)milliliters * (uint64_t)s_ctx.cfg.ms_per_ml;
    if (duration_ms == 0) {
        return;
    }

    const TickType_t duration_ticks =
        pdMS_TO_TICKS(duration_ms > UINT32_MAX ? UINT32_MAX : duration_ms);
    ESP_LOGI(TAG,
             "%s dosing %u ml (~%llu ms)",
             channel->name,
             (unsigned)milliliters,
             (unsigned long long)duration_ms);
    gpio_set_level(channel->relay_gpio, 1);
    vTaskDelay(duration_ticks == 0 ? 1 : duration_ticks);
    gpio_set_level(channel->relay_gpio, 0);
    ESP_LOGI(TAG, "%s finished", channel->name);
}

esp_err_t doser_publish(const doser_command_t *command)
{
    if (!command) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (command->index >= CONFIG_DOSER_COUNT) {
        ESP_LOGW(TAG, "Invalid doser index %u", (unsigned)command->index);
        return ESP_ERR_INVALID_ARG;
    }

    doser_channel_t *channel = &s_ctx.channels[command->index];
    if (!channel->queue) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueSend(channel->queue, &command->milliliters, 0) != pdTRUE) {
        ESP_LOGW(TAG, "%s queue full, dropping dose request", channel->name);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}
