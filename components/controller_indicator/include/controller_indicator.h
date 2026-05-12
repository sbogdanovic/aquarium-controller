#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t data_gpio;
    uint32_t pixel_count;
    uint8_t brightness;
} controller_indicator_config_t;

esp_err_t controller_indicator_start(const controller_indicator_config_t *config);

typedef enum {
    CONTROLLER_INDICATOR_COMMAND_PUMP_ON = 0,
    CONTROLLER_INDICATOR_COMMAND_PUMP_OFF,
    CONTROLLER_INDICATOR_COMMAND_SENSOR_FAULT,
} controller_indicator_command_t;

esp_err_t controller_indicator_publish(controller_indicator_command_t command);

#ifdef __cplusplus
}
#endif
