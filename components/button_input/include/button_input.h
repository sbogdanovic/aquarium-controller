#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t button_gpio;
} button_input_config_t;

esp_err_t button_input_start(const button_input_config_t *config);

typedef enum {
    BUTTON_INPUT_EVENT_PRESSED = 0,
} button_input_event_t;

#define BUTTON_INPUT_EVENT_QUEUE_LEN 4

esp_err_t button_input_subscribe(QueueHandle_t *queue);
const char *button_input_event_name(button_input_event_t event);

#ifdef __cplusplus
}
#endif
