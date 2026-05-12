#include "button_input.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stddef.h>

#define BUTTON_TASK_STACK    2048
#define BUTTON_TASK_PRIORITY 4
#define BUTTON_ISR_QUEUE_LEN 4
#define BUTTON_DEBOUNCE_MS   50

typedef struct {
    button_input_config_t cfg;
    QueueHandle_t isr_queue;
    QueueHandle_t event_queue;
    bool active_low;
} button_input_ctx_t;

static const char *TAG = "button_input";
static button_input_ctx_t s_ctx;
static bool s_started;
static bool s_isr_service_installed;

static void button_task(void *arg);
static void button_isr(void *arg);
static esp_err_t configure_button_gpio(gpio_num_t gpio);
static void publish_button_event(button_input_event_t event);

static const char *const BUTTON_EVENT_NAMES[] = {
    [BUTTON_INPUT_EVENT_PRESSED] = "PRESSED",
};

esp_err_t button_input_start(const button_input_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->button_gpio < 0) {
        ESP_LOGI(TAG, "Button disabled by configuration");
        return ESP_OK;
    }

    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.cfg = *config;
    s_ctx.isr_queue = xQueueCreate(BUTTON_ISR_QUEUE_LEN, sizeof(uint32_t));
    if (!s_ctx.isr_queue) {
        return ESP_ERR_NO_MEM;
    }

    s_ctx.event_queue = xQueueCreate(BUTTON_INPUT_EVENT_QUEUE_LEN, sizeof(button_input_event_t));
    if (!s_ctx.event_queue) {
        vQueueDelete(s_ctx.isr_queue);
        s_ctx.isr_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(configure_button_gpio(s_ctx.cfg.button_gpio), TAG, "gpio config failed");

    if (xTaskCreate(
            button_task, "btn-handler", BUTTON_TASK_STACK, &s_ctx, BUTTON_TASK_PRIORITY, NULL) !=
        pdPASS) {
        vQueueDelete(s_ctx.isr_queue);
        s_ctx.isr_queue = NULL;
        vQueueDelete(s_ctx.event_queue);
        s_ctx.event_queue = NULL;
        return ESP_FAIL;
    }

    s_started = true;
    ESP_LOGI(TAG, "Button input initialized on GPIO%d", s_ctx.cfg.button_gpio);
    return ESP_OK;
}

static esp_err_t configure_button_gpio(gpio_num_t gpio)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio config failed");

    if (!s_isr_service_installed) {
        esp_err_t err = gpio_install_isr_service(0);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            s_isr_service_installed = true;
        } else {
            ESP_RETURN_ON_ERROR(err, TAG, "isr service");
        }
    }

    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(gpio, button_isr, &s_ctx), TAG, "isr add");
    s_ctx.active_low = true;
    return ESP_OK;
}

static void button_task(void *arg)
{
    button_input_ctx_t *ctx = (button_input_ctx_t *)arg;
    uint32_t timestamp = 0;

    while (true) {
        if (xQueueReceive(ctx->isr_queue, &timestamp, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
        const int level = gpio_get_level(ctx->cfg.button_gpio);
        if (ctx->active_low && level != 0) {
            continue;
        }

        ESP_LOGI(TAG, "On-board button pressed");
        publish_button_event(BUTTON_INPUT_EVENT_PRESSED);
    }
}

static void IRAM_ATTR button_isr(void *arg)
{
    button_input_ctx_t *ctx = (button_input_ctx_t *)arg;
    if (!ctx || !ctx->isr_queue) {
        return;
    }

    const uint32_t now = (uint32_t)xTaskGetTickCountFromISR();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(ctx->isr_queue, &now, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void publish_button_event(button_input_event_t event)
{
    if (!s_ctx.event_queue) {
        return;
    }

    if (xQueueSend(s_ctx.event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Button event queue full, dropping %s", button_input_event_name(event));
    }
}

esp_err_t button_input_subscribe(QueueHandle_t *queue)
{
    if (!queue) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.event_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    *queue = s_ctx.event_queue;
    return ESP_OK;
}

const char *button_input_event_name(button_input_event_t event)
{
    const size_t index = (size_t)event;
    if (index < (sizeof(BUTTON_EVENT_NAMES) / sizeof(BUTTON_EVENT_NAMES[0])) &&
        BUTTON_EVENT_NAMES[index]) {
        return BUTTON_EVENT_NAMES[index];
    }
    return "INVALID_BUTTON_EVENT";
}
