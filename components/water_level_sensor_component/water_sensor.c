#include "water_sensor.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include <stddef.h>

#define WATER_LEVEL_SENSOR_TASK_STACK 4096
#define WATER_LEVEL_SENSOR_TASK_PRIO  4
#define SAMPLE_DELAY_MS         2
#define DISCHARGE_DELAY_MS      5

typedef struct {
    water_sensor_config_t cfg;
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t cali_handle;
    bool calibration_enabled;
    gpio_num_t sensor_gpio;
    bool sensor_gpio_valid;
    water_sensor_event_t last_event;
    QueueHandle_t event_queue;
} water_sensor_ctx_t;

static const char *TAG = "water_sensor";
static water_sensor_ctx_t s_ctx;
static bool s_started;

static esp_err_t water_sensor_configure_adc(water_sensor_ctx_t *ctx);
static esp_err_t water_sensor_configure_sensor_gpio(water_sensor_ctx_t *ctx);
static esp_err_t water_sensor_read_mv(water_sensor_ctx_t *ctx, int *reading_mv);
static esp_err_t water_sensor_read_level(water_sensor_ctx_t *ctx, int *reading_level);
static water_sensor_event_t classify_reading(const water_sensor_ctx_t *ctx, int reading_mv);
static water_sensor_event_t classify_digital(const water_sensor_ctx_t *ctx, int reading_level);
static void water_sensor_task(void *param);
static void publish_sensor_event(water_sensor_event_t event);
static void water_sensor_disable_discharge(const water_sensor_ctx_t *ctx);
static void water_sensor_enable_discharge(const water_sensor_ctx_t *ctx);

static const char *const WATER_LEVEL_SENSOR_EVENT_NAMES[] = {
    [WATER_LEVEL_SENSOR_EVENT_LOW] = "LOW",
    [WATER_LEVEL_SENSOR_EVENT_OK] = "OK",
    [WATER_LEVEL_SENSOR_EVENT_UNKNOWN] = "UNKNOWN",
};

esp_err_t water_sensor_start(const water_sensor_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.cfg = *config;
    s_ctx.last_event = WATER_LEVEL_SENSOR_EVENT_UNKNOWN;
    s_ctx.event_queue = xQueueCreate(WATER_LEVEL_SENSOR_EVENT_QUEUE_LEN, sizeof(water_sensor_event_t));
    if (!s_ctx.event_queue) {
        return ESP_ERR_NO_MEM;
    }

    if (s_ctx.cfg.use_digital_input) {
        ESP_RETURN_ON_ERROR(water_sensor_configure_sensor_gpio(&s_ctx), TAG,
                            "Failed to configure sensor GPIO");
    } else {
        ESP_RETURN_ON_ERROR(water_sensor_configure_adc(&s_ctx), TAG, "Failed to configure ADC");
        esp_err_t gpio_err = water_sensor_configure_sensor_gpio(&s_ctx);
        if (gpio_err != ESP_OK) {
            ESP_LOGW(TAG, "Sensor GPIO unavailable: %s", esp_err_to_name(gpio_err));
        }
    }

    if (xTaskCreate(water_sensor_task, "water-sensor", WATER_LEVEL_SENSOR_TASK_STACK, &s_ctx,
                    WATER_LEVEL_SENSOR_TASK_PRIO, NULL) != pdPASS) {
        vQueueDelete(s_ctx.event_queue);
        s_ctx.event_queue = NULL;
        return ESP_FAIL;
    }

    s_started = true;
    ESP_LOGI(TAG, "Water sensor task started");
    return ESP_OK;
}

static esp_err_t water_sensor_configure_adc(water_sensor_ctx_t *ctx)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ctx->cfg.unit,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_cfg, &ctx->adc_handle), TAG, "new unit failed");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ctx->cfg.bitwidth,
        .atten = ctx->cfg.atten,
    };
    ESP_RETURN_ON_ERROR(
        adc_oneshot_config_channel(ctx->adc_handle, ctx->cfg.channel, &chan_cfg), TAG,
        "channel config failed");

#if SOC_ADC_CALIBRATION_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ctx->cfg.unit,
        .atten = ctx->cfg.atten,
        .bitwidth = ctx->cfg.bitwidth,
    };
    esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &ctx->cali_handle);
    if (ret == ESP_OK) {
        ctx->calibration_enabled = true;
    } else {
        ctx->calibration_enabled = false;
        ESP_LOGW(TAG, "ADC calibration unavailable (%s), raw values used", esp_err_to_name(ret));
    }
#else
    ctx->calibration_enabled = false;
#endif

    return ESP_OK;
}

static esp_err_t water_sensor_read_mv(water_sensor_ctx_t *ctx, int *reading_mv)
{
    int total = 0;
    esp_err_t ret = ESP_OK;

    for (uint32_t i = 0; i < ctx->cfg.sample_count; ++i) {
        water_sensor_enable_discharge(ctx);
        vTaskDelay(pdMS_TO_TICKS(DISCHARGE_DELAY_MS));
        water_sensor_disable_discharge(ctx);

        int raw = 0;
        ESP_GOTO_ON_ERROR(adc_oneshot_read(ctx->adc_handle, ctx->cfg.channel, &raw), cleanup, TAG,
                          "adc read failed");
        if (ctx->calibration_enabled) {
            int mv = 0;
            ESP_GOTO_ON_ERROR(adc_cali_raw_to_voltage(ctx->cali_handle, raw, &mv), cleanup, TAG,
                              "calibration failed");
            total += mv;
        } else {
            total += raw;
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));
    }

    *reading_mv = total / (int)ctx->cfg.sample_count;
    ret = ESP_OK;

cleanup:
    water_sensor_enable_discharge(ctx);
    return ret;
}

static esp_err_t water_sensor_read_level(water_sensor_ctx_t *ctx, int *reading_level)
{
    if (!ctx->sensor_gpio_valid) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx->cfg.sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t high_count = 0;
    for (uint32_t i = 0; i < ctx->cfg.sample_count; ++i) {
        int level = gpio_get_level(ctx->sensor_gpio);
        if (level < 0) {
            return ESP_FAIL;
        }
        if (level > 0) {
            ++high_count;
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));
    }

    if ((ctx->cfg.sample_count % 2 == 0) && (high_count * 2 == ctx->cfg.sample_count)) {
        *reading_level = -1;
    } else {
        *reading_level = (high_count * 2 > ctx->cfg.sample_count) ? 1 : 0;
    }
    return ESP_OK;
}

static water_sensor_event_t classify_reading(const water_sensor_ctx_t *ctx, int reading_mv)
{
    if (reading_mv == 0) {
        return WATER_LEVEL_SENSOR_EVENT_LOW;
    }

    if (reading_mv > 0 && reading_mv < (int)ctx->cfg.disconnect_mv) {
        return WATER_LEVEL_SENSOR_EVENT_UNKNOWN;
    }

    if (reading_mv >= (int)ctx->cfg.threshold_mv) {
        return WATER_LEVEL_SENSOR_EVENT_OK;
    }

    return WATER_LEVEL_SENSOR_EVENT_UNKNOWN;
}

static void water_sensor_task(void *param)
{
    water_sensor_ctx_t *ctx = (water_sensor_ctx_t *)param;

    while (true) {
        int reading_value = 0;
        water_sensor_event_t evt = WATER_LEVEL_SENSOR_EVENT_UNKNOWN;
        esp_err_t err = ESP_OK;

        if (ctx->cfg.use_digital_input) {
            err = water_sensor_read_level(ctx, &reading_value);
            if (err == ESP_OK) {
                evt = classify_digital(ctx, reading_value);
                ESP_LOGI(TAG, "Reading level %d -> %s", reading_value,
                         water_sensor_event_name(evt));
            } else {
                ESP_LOGW(TAG, "Digital sensor read failed: %s", esp_err_to_name(err));
                evt = WATER_LEVEL_SENSOR_EVENT_UNKNOWN;
            }
        } else {
            err = water_sensor_read_mv(ctx, &reading_value);
            if (err == ESP_OK) {
                evt = classify_reading(ctx, reading_value);
                ESP_LOGI(TAG, "Reading %d mV -> %s", reading_value,
                         water_sensor_event_name(evt));
            } else {
                ESP_LOGW(TAG, "Sensor read failed: %s", esp_err_to_name(err));
                evt = WATER_LEVEL_SENSOR_EVENT_UNKNOWN;
            }
        }

        if (evt != ctx->last_event) {
            ctx->last_event = evt;
            publish_sensor_event(evt);
        }

        vTaskDelay(pdMS_TO_TICKS(ctx->cfg.poll_period_ms));
    }
}

static void publish_sensor_event(water_sensor_event_t event)
{
    if (!s_ctx.event_queue) {
        return;
    }

    if (xQueueSend(s_ctx.event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Water sensor queue full, dropping %s", water_sensor_event_name(event));
    }
}

esp_err_t water_sensor_subscribe(QueueHandle_t *queue)
{
    if (!queue) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_started || !s_ctx.event_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    *queue = s_ctx.event_queue;
    return ESP_OK;
}

const char *water_sensor_event_name(water_sensor_event_t event)
{
    const size_t index = (size_t)event;
    if (index < (sizeof(WATER_LEVEL_SENSOR_EVENT_NAMES) / sizeof(WATER_LEVEL_SENSOR_EVENT_NAMES[0])) &&
        WATER_LEVEL_SENSOR_EVENT_NAMES[index]) {
        return WATER_LEVEL_SENSOR_EVENT_NAMES[index];
    }
    return "INVALID_WATER_LEVEL_SENSOR_EVENT";
}

static water_sensor_event_t classify_digital(const water_sensor_ctx_t *ctx, int reading_level)
{
    if (reading_level < 0) {
        return WATER_LEVEL_SENSOR_EVENT_UNKNOWN;
    }

    const int active_level = ctx->cfg.digital_active_high ? 1 : 0;
    if (reading_level == active_level) {
        return WATER_LEVEL_SENSOR_EVENT_OK;
    }
    return WATER_LEVEL_SENSOR_EVENT_LOW;
}

static esp_err_t water_sensor_configure_sensor_gpio(water_sensor_ctx_t *ctx)
{
    int io_num = -1;
    esp_err_t err = adc_oneshot_channel_to_io(ctx->cfg.unit, ctx->cfg.channel, &io_num);
    if (err != ESP_OK) {
        ctx->sensor_gpio_valid = false;
        return err;
    }

    ctx->sensor_gpio = (gpio_num_t)io_num;
    ctx->sensor_gpio_valid = true;

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << ctx->sensor_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (ctx->cfg.use_digital_input) {
        cfg.pull_up_en = ctx->cfg.digital_pullup_en ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = ctx->cfg.digital_pulldown_en ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    } else {
        cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    }

    return gpio_config(&cfg);
}

static void water_sensor_disable_discharge(const water_sensor_ctx_t *ctx)
{
    if (!ctx->sensor_gpio_valid || ctx->cfg.use_digital_input) {
        return;
    }

    esp_err_t err = gpio_set_direction(ctx->sensor_gpio, GPIO_MODE_DISABLE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to disable digital path on GPIO%d: %s", ctx->sensor_gpio,
                 esp_err_to_name(err));
        return;
    }

    err = gpio_pulldown_dis(ctx->sensor_gpio);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to release pulldown on GPIO%d: %s", ctx->sensor_gpio,
                 esp_err_to_name(err));
    }
}

static void water_sensor_enable_discharge(const water_sensor_ctx_t *ctx)
{
    if (!ctx->sensor_gpio_valid || ctx->cfg.use_digital_input) {
        return;
    }

    esp_err_t err = gpio_set_direction(ctx->sensor_gpio, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to drive GPIO%d low: %s", ctx->sensor_gpio, esp_err_to_name(err));
        return;
    }

    err = gpio_set_level(ctx->sensor_gpio, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to pull GPIO%d low: %s", ctx->sensor_gpio, esp_err_to_name(err));
    }
}
