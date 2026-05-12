#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "hal/adc_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    adc_unit_t unit;
    adc_channel_t channel;
    adc_bitwidth_t bitwidth;
    adc_atten_t atten;
    uint32_t sample_count;
    uint32_t poll_period_ms;
    uint32_t threshold_mv;
    uint32_t disconnect_mv;
    bool use_digital_input;
    bool digital_active_high;
    bool digital_pullup_en;
    bool digital_pulldown_en;
} water_sensor_config_t;

esp_err_t water_sensor_start(const water_sensor_config_t *config);

typedef enum {
    WATER_LEVEL_SENSOR_EVENT_LOW = 0,
    WATER_LEVEL_SENSOR_EVENT_OK,
    WATER_LEVEL_SENSOR_EVENT_UNKNOWN,
} water_sensor_event_t;

#define WATER_LEVEL_SENSOR_EVENT_QUEUE_LEN 8

esp_err_t water_sensor_subscribe(QueueHandle_t *queue);
const char *water_sensor_event_name(water_sensor_event_t event);

#ifdef __cplusplus
}
#endif
