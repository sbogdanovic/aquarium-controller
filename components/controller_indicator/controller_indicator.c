#include "controller_indicator.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
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
#define WS2812_RMT_RES_HZ      (10 * 1000 * 1000) // 10 MHz, 1 tick = 0.1 µs

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} indicator_color_t;

typedef struct {
    controller_indicator_config_t cfg;
    QueueHandle_t queue;
    rmt_channel_handle_t rmt_channel;
    rmt_encoder_handle_t rmt_encoder;
    uint8_t *pixel_buf;
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

    s_ctx.pixel_buf = calloc(s_ctx.cfg.pixel_count, 3);
    if (!s_ctx.pixel_buf) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(indicator_configure_rmt(&s_ctx), TAG, "rmt init failed");

    s_ctx.queue = xQueueCreate(INDICATOR_QUEUE_LEN, sizeof(controller_indicator_command_t));
    if (!s_ctx.queue) {
        indicator_deinit_rmt(&s_ctx);
        free(s_ctx.pixel_buf);
        s_ctx.pixel_buf = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(indicator_task, "water-led", INDICATOR_TASK_STACK, &s_ctx,
                    INDICATOR_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        indicator_deinit_rmt(&s_ctx);
        free(s_ctx.pixel_buf);
        s_ctx.pixel_buf = NULL;
        return ESP_FAIL;
    }

    s_started = true;
    ESP_LOGI(TAG, "LED indicator task started on GPIO%d", s_ctx.cfg.data_gpio);
    return ESP_OK;
}

static esp_err_t indicator_configure_rmt(controller_indicator_ctx_t *ctx)
{
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = ctx->cfg.data_gpio,
        .mem_block_symbols = 64,
        .resolution_hz = WS2812_RMT_RES_HZ,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_chan_config, &ctx->rmt_channel), TAG,
                        "new tx channel failed");

    // WS2812 timing at 10 MHz (0.1 µs per tick):
    // Bit 0: high 0.4 µs (4 ticks), low 0.85 µs (8 ticks)
    // Bit 1: high 0.8 µs (8 ticks), low 0.45 µs (4 ticks)
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .duration0 = 4,
            .level0 = 1,
            .duration1 = 8,
            .level1 = 0,
        },
        .bit1 = {
            .duration0 = 8,
            .level0 = 1,
            .duration1 = 4,
            .level1 = 0,
        },
        .flags.msb_first = true,
    };
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &ctx->rmt_encoder), TAG,
                        "new bytes encoder failed");

    ESP_RETURN_ON_ERROR(rmt_enable(ctx->rmt_channel), TAG, "enable channel failed");

    return ESP_OK;
}

static void indicator_deinit_rmt(controller_indicator_ctx_t *ctx)
{
    if (ctx->rmt_channel) {
        rmt_disable(ctx->rmt_channel);
        rmt_del_channel(ctx->rmt_channel);
        ctx->rmt_channel = NULL;
    }
    if (ctx->rmt_encoder) {
        rmt_del_encoder(ctx->rmt_encoder);
        ctx->rmt_encoder = NULL;
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
    if (!ctx->rmt_channel || !ctx->rmt_encoder || !ctx->pixel_buf) {
        return;
    }

    const uint8_t r = scale_channel(ctx, color->r);
    const uint8_t g = scale_channel(ctx, color->g);
    const uint8_t b = scale_channel(ctx, color->b);

    // WS2812 expects GRB byte order
    for (uint32_t pixel = 0; pixel < ctx->cfg.pixel_count; ++pixel) {
        ctx->pixel_buf[pixel * 3 + 0] = g;
        ctx->pixel_buf[pixel * 3 + 1] = r;
        ctx->pixel_buf[pixel * 3 + 2] = b;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags.eot_level = 0, // low level after transmission acts as WS2812 reset
    };
    esp_err_t err = rmt_transmit(ctx->rmt_channel, ctx->rmt_encoder, ctx->pixel_buf,
                                 ctx->cfg.pixel_count * 3, &tx_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LED transmit failed: %s", esp_err_to_name(err));
        return;
    }
    rmt_tx_wait_all_done(ctx->rmt_channel, 100);
}

static uint8_t scale_channel(const controller_indicator_ctx_t *ctx, uint8_t value)
{
    if (ctx->cfg.brightness >= 255) {
        return value;
    }
    return (uint8_t)((value * ctx->cfg.brightness) / 255);
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
