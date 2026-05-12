#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t relay_gpios[CONFIG_DOSER_COUNT];
    uint32_t ms_per_ml;
} doser_config_t;

esp_err_t doser_start(const doser_config_t *config);

typedef struct {
    uint32_t index;
    uint32_t milliliters;
} doser_command_t;

esp_err_t doser_publish(const doser_command_t *command);

#ifdef __cplusplus
}
#endif
