#include "controller_indicator.h"

#include "driver/rmt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define INDICATOR_TASK_STACK    2048
#define INDICATOR_TASK_PRIORITY 4
#define INDICATOR_QUEUE_LEN     4
#define DEFAULT_PIXEL_COUNT     1
#define DEFAULT_BRIGHTNESS      32
#define INDICATOR_RMT_CHANNEL   RMT_CHANNEL_0
#define WS2812_CLK_DIV          4
#define WS2812_T0H              8    // 0.4us @ 20MHz
#define WS2812_T0L              17   // 0.85us @ 20MHz
#define WS2812_T1H              16   // 0.8us @ 20MHz
#define WS2812_T1L              9    // 0.45us @ 20MHz
#define WS2812_RESET_TICKS      1000 // 50us reset pulse

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} indicator_color_t;

typedef struct {
    controller_indicator_config_t cfg;
    QueueHandle_t queue;
    rmt_channel_t channel;
    bool fault_latched;
} controller_indicator_ctx_t;

static const char *TAG = "controller_indicator";
static const char *const INDICATOR_COMMAND_NAMES[] = {
    [CONTROLLER_INDICATOR_COMMAND_PUMP_ON] = "PUMP_ON",
    [CONTROLLER_INDICATOR_COMMAND_PUMP_OFF] = "PUMP_OFF",
    [CONTROLLER_INDICATOR_COMMAND_SENSOR_FAULT] = "SENSOR_FAULT",
};

static const indicator_color_t COLOR_LOW = {255, 0, 0};
static const indicator_color_t COLOR_OK = {0, 255, 32};
static const indicator_color_t COLOR_FAULT = {255, 140, 0};
static const indicator_color_t COLOR_OFF = {0, 0, 0};

static controller_indicator_ctx_t s_ctx;
static bool s_started;

static esp_err_t indicator_configure_rmt(controller_indicator_ctx_t *ctx);
static void indicator_deinit_rmt(controller_indicator_ctx_t *ctx);
static void indicator_task(void *arg);
static void apply_color(controller_indicator_ctx_t *ctx, const indicator_color_t *color);
static uint8_t scale_channel(const controller_indicator_ctx_t *ctx, uint8_t value);
static rmt_item32_t *encode_byte(rmt_item32_t *item, uint8_t byte);

esp_err_t controller_indicator_start(const controller_indicator_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->data_gpio < 0) {
        ESP_LOGI(TAG, "Indicator disabled by configuration");
        return ESP_OK;
    }

    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.cfg = *config;
    if (s_ctx.cfg.pixel_count == 0) {
        s_ctx.cfg.pixel_count = DEFAULT_PIXEL_COUNT;
    }
    if (s_ctx.cfg.brightness == 0) {
        s_ctx.cfg.brightness = DEFAULT_BRIGHTNESS;
    }
    s_ctx.channel = RMT_CHANNEL_MAX;

    ESP_RETURN_ON_ERROR(indicator_configure_rmt(&s_ctx), TAG, "rmt init failed");

    s_ctx.queue = xQueueCreate(INDICATOR_QUEUE_LEN, sizeof(controller_indicator_command_t));
    if (!s_ctx.queue) {
        indicator_deinit_rmt(&s_ctx);
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(indicator_task, "water-led", INDICATOR_TASK_STACK, &s_ctx,
                    INDICATOR_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        indicator_deinit_rmt(&s_ctx);
        return ESP_FAIL;
    }

    s_started = true;
    ESP_LOGI(TAG, "LED indicator task started on GPIO%d", s_ctx.cfg.data_gpio);
    return ESP_OK;
}

static esp_err_t indicator_configure_rmt(controller_indicator_ctx_t *ctx)
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(ctx->cfg.data_gpio, INDICATOR_RMT_CHANNEL);
    config.clk_div = WS2812_CLK_DIV;
    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    err = rmt_driver_install(config.channel, 0, 0);
    if (err == ESP_OK) {
        ctx->channel = config.channel;
    }
    return err;
}

static void indicator_deinit_rmt(controller_indicator_ctx_t *ctx)
{
    if (ctx->channel < RMT_CHANNEL_MAX) {
        rmt_driver_uninstall(ctx->channel);
        ctx->channel = RMT_CHANNEL_MAX;
    }
}

static void indicator_task(void *arg)
{
    controller_indicator_ctx_t *ctx = (controller_indicator_ctx_t *)arg;
    controller_indicator_command_t command;

    apply_color(ctx, &COLOR_OFF);

    while (true) {
        if (xQueueReceive(ctx->queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (command) {
        case CONTROLLER_INDICATOR_COMMAND_PUMP_ON:
            ctx->fault_latched = false;
            apply_color(ctx, &COLOR_LOW);
            break;
        case CONTROLLER_INDICATOR_COMMAND_PUMP_OFF:
            if (ctx->fault_latched) {
                apply_color(ctx, &COLOR_FAULT);
            } else {
                apply_color(ctx, &COLOR_OK);
            }
            break;
        case CONTROLLER_INDICATOR_COMMAND_SENSOR_FAULT:
            ctx->fault_latched = true;
            apply_color(ctx, &COLOR_FAULT);
            break;
        default:
            break;
        }
    }
}

static void apply_color(controller_indicator_ctx_t *ctx, const indicator_color_t *color)
{
    if (ctx->channel >= RMT_CHANNEL_MAX) {
        return;
    }

    const uint8_t r = scale_channel(ctx, color->r);
    const uint8_t g = scale_channel(ctx, color->g);
    const uint8_t b = scale_channel(ctx, color->b);

    const size_t bit_count = ctx->cfg.pixel_count * 24;
    const size_t item_count = bit_count + 1;
    rmt_item32_t *items = calloc(item_count, sizeof(rmt_item32_t));
    if (!items) {
        ESP_LOGW(TAG, "LED buffer allocation failed");
        return;
    }

    rmt_item32_t *cursor = items;
    for (uint32_t pixel = 0; pixel < ctx->cfg.pixel_count; ++pixel) {
        cursor = encode_byte(cursor, g);
        cursor = encode_byte(cursor, r);
        cursor = encode_byte(cursor, b);
    }

    rmt_item32_t *reset_item = &items[bit_count];
    reset_item->duration0 = 0;
    reset_item->level0 = 0;
    reset_item->duration1 = WS2812_RESET_TICKS;
    reset_item->level1 = 0;

    esp_err_t err = rmt_write_items(ctx->channel, items, item_count, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LED refresh failed: %s", esp_err_to_name(err));
    }

    free(items);
}

static uint8_t scale_channel(const controller_indicator_ctx_t *ctx, uint8_t value)
{
    if (ctx->cfg.brightness >= 255) {
        return value;
    }
    return (uint8_t)((value * ctx->cfg.brightness) / 255);
}

static rmt_item32_t *encode_byte(rmt_item32_t *item, uint8_t byte)
{
    for (int bit = 7; bit >= 0; --bit) {
        const bool set = ((byte >> bit) & 0x1) != 0;
        if (set) {
            item->duration0 = WS2812_T1H;
            item->level0 = 1;
            item->duration1 = WS2812_T1L;
            item->level1 = 0;
        } else {
            item->duration0 = WS2812_T0H;
            item->level0 = 1;
            item->duration1 = WS2812_T0L;
            item->level1 = 0;
        }
        ++item;
    }
    return item;
}

esp_err_t controller_indicator_publish(controller_indicator_command_t command)
{
    if (!s_started || !s_ctx.queue) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueSend(s_ctx.queue, &command, 0) != pdTRUE) {
        const size_t index = (size_t)command;
        const char *name = (index < (sizeof(INDICATOR_COMMAND_NAMES) / sizeof(INDICATOR_COMMAND_NAMES[0])) &&
                           INDICATOR_COMMAND_NAMES[index])
                              ? INDICATOR_COMMAND_NAMES[index]
                              : "UNKNOWN";
        ESP_LOGW(TAG, "Indicator queue full, dropping %s", name);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}
