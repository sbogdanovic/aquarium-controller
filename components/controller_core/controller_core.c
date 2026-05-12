#include "controller_core.h"
#include "controller_indicator.h"
#include "water_pump.h"
#include "water_sensor.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define CONTROLLER_TASK_STACK_SIZE 4096
#define CONTROLLER_TASK_PRIORITY   5

typedef enum {
    CONTROLLER_STATE_IDLE = 0,
    CONTROLLER_STATE_TOP_UP,
} controller_state_t;

static const char *TAG = "controller_core";

static QueueHandle_t s_water_sensor_queue;
static TaskHandle_t s_task_handle;
static controller_state_t s_state = CONTROLLER_STATE_IDLE;

static void controller_core_task(void *args);
static void handle_water_sensor_event(water_sensor_event_t event_id);
static const char *controller_state_name(controller_state_t state);
static void publish_pump_command(water_pump_command_t command);
static void publish_indicator_command(controller_indicator_command_t command);
static void publish_fault(void);

esp_err_t controller_core_init(void)
{
    if (s_task_handle) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(
        water_sensor_subscribe(&s_water_sensor_queue), TAG, "water sensor subscribe failed");

    if (xTaskCreate(controller_core_task,
                    "ctrl-core",
                    CONTROLLER_TASK_STACK_SIZE,
                    NULL,
                    CONTROLLER_TASK_PRIORITY,
                    &s_task_handle) != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Controller core task started");
    return ESP_OK;
}

static void controller_core_task(void *args)
{
    while (true) {
        water_sensor_event_t event_id;
        if (xQueueReceive(s_water_sensor_queue, &event_id, portMAX_DELAY) == pdTRUE) {
            handle_water_sensor_event(event_id);
        }
    }
}

static void handle_water_sensor_event(water_sensor_event_t event_id)
{
    controller_state_t new_state = s_state;

    switch (event_id) {
    case WATER_LEVEL_SENSOR_EVENT_LOW:
        new_state = CONTROLLER_STATE_TOP_UP;
        if (new_state != s_state) {
            publish_pump_command(WATER_PUMP_COMMAND_ENABLE);
            publish_indicator_command(CONTROLLER_INDICATOR_COMMAND_PUMP_ON);
        }
        break;
    case WATER_LEVEL_SENSOR_EVENT_OK:
        new_state = CONTROLLER_STATE_IDLE;
        if (new_state != s_state) {
            publish_pump_command(WATER_PUMP_COMMAND_DISABLE);
            publish_indicator_command(CONTROLLER_INDICATOR_COMMAND_PUMP_OFF);
        }
        break;
    case WATER_LEVEL_SENSOR_EVENT_UNKNOWN:
    default:
        new_state = CONTROLLER_STATE_IDLE;
        if (new_state != s_state) {
            publish_pump_command(WATER_PUMP_COMMAND_DISABLE);
            publish_fault();
        }
        break;
    }

    if (new_state != s_state) {
        ESP_LOGI(TAG,
                 "State transition %s -> %s",
                 controller_state_name(s_state),
                 controller_state_name(new_state));
        s_state = new_state;
    }
}

static const char *controller_state_name(controller_state_t state)
{
    switch (state) {
    case CONTROLLER_STATE_IDLE:
        return "OK";
    case CONTROLLER_STATE_TOP_UP:
        return "LOW";
    default:
        return "INVALID";
    }
}

static void publish_pump_command(water_pump_command_t command)
{
    esp_err_t err = water_pump_publish(command);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Pump publish failed for %s (%s)",
                 water_pump_command_name(command),
                 esp_err_to_name(err));
    }
}

static void publish_indicator_command(controller_indicator_command_t command)
{
    esp_err_t err = controller_indicator_publish(command);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Indicator publish failed (%s)", esp_err_to_name(err));
    }
}

static void publish_fault(void)
{
    publish_pump_command(WATER_PUMP_COMMAND_SENSOR_FAULT);
    publish_indicator_command(CONTROLLER_INDICATOR_COMMAND_SENSOR_FAULT);
}
