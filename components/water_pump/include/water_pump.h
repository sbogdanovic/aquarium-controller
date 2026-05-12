#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t relay_gpio;
} water_pump_config_t;

esp_err_t water_pump_start(const water_pump_config_t *config);

typedef enum {
    WATER_PUMP_COMMAND_ENABLE = 0,
    WATER_PUMP_COMMAND_DISABLE,
    WATER_PUMP_COMMAND_SENSOR_FAULT,
} water_pump_command_t;

esp_err_t water_pump_publish(water_pump_command_t command);
const char *water_pump_command_name(water_pump_command_t command);

#ifdef __cplusplus
}
#endif
