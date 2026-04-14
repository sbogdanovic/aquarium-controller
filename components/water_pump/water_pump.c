#include "water_pump.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stddef.h>

#define WATER_PUMP_TASK_STACK 2048
#define WATER_PUMP_TASK_PRIO  4
#define WATER_PUMP_QUEUE_LEN  4

typedef struct {
    water_pump_config_t cfg;
    QueueHandle_t queue;
    bool active;
} water_pump_ctx_t;

static const char *TAG = "water_pump";
static water_pump_ctx_t s_ctx;
static bool s_started;

static esp_err_t configure_output_gpio(gpio_num_t pin);
static void set_relay_level(bool enable);
static void water_pump_task(void *arg);
static const char *const PUMP_COMMAND_NAMES[] = {
    [WATER_PUMP_COMMAND_ENABLE] = "ENABLE",
    [WATER_PUMP_COMMAND_DISABLE] = "DISABLE",
    [WATER_PUMP_COMMAND_SENSOR_FAULT] = "SENSOR_FAULT",
};

esp_err_t water_pump_start(const water_pump_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.cfg = *config;
    s_ctx.active = false;
    s_ctx.queue = xQueueCreate(WATER_PUMP_QUEUE_LEN, sizeof(water_pump_command_t));
    if (!s_ctx.queue) {
        return ESP_ERR_NO_MEM;
    }

    if (s_ctx.cfg.relay_gpio >= 0) {
        ESP_RETURN_ON_ERROR(configure_output_gpio(s_ctx.cfg.relay_gpio), TAG, "relay gpio");
    }

    if (xTaskCreate(water_pump_task, "water-pump", WATER_PUMP_TASK_STACK, &s_ctx,
                    WATER_PUMP_TASK_PRIO, NULL) != pdPASS) {
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ESP_FAIL;
    }

    s_started = true;
    ESP_LOGI(TAG, "Water pump task started");
    return ESP_OK;
}

static esp_err_t configure_output_gpio(gpio_num_t pin)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, 0), TAG, "gpio_set_level failed");
    return ESP_OK;
}

static void set_relay_level(bool enable)
{
    if (s_ctx.cfg.relay_gpio < 0) {
        return;
    }

    const int level = enable ? 1 : 0;
    gpio_set_level(s_ctx.cfg.relay_gpio, level);
}

static void water_pump_task(void *arg)
{
    water_pump_ctx_t *ctx = (water_pump_ctx_t *)arg;
    water_pump_command_t command;

    while (true) {
        if (xQueueReceive(ctx->queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (command) {
        case WATER_PUMP_COMMAND_ENABLE:
            ctx->active = true;
            set_relay_level(true);
            ESP_LOGI(TAG, "Pump enabled");
            break;
        case WATER_PUMP_COMMAND_DISABLE:
            ctx->active = false;
            set_relay_level(false);
            ESP_LOGI(TAG, "Pump disabled");
            break;
        case WATER_PUMP_COMMAND_SENSOR_FAULT:
            ctx->active = false;
            set_relay_level(false);
            ESP_LOGW(TAG, "Sensor fault detected, pump forced off");
            break;
        default:
            break;
        }
    }
}

esp_err_t water_pump_publish(water_pump_command_t command)
{
    if (!s_started || !s_ctx.queue) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueSend(s_ctx.queue, &command, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Pump queue full, dropping %s", water_pump_command_name(command));
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

const char *water_pump_command_name(water_pump_command_t command)
{
    const size_t index = (size_t)command;
    if (index < (sizeof(PUMP_COMMAND_NAMES) / sizeof(PUMP_COMMAND_NAMES[0])) &&
        PUMP_COMMAND_NAMES[index]) {
        return PUMP_COMMAND_NAMES[index];
    }
    return "INVALID_PUMP_COMMAND";
}
