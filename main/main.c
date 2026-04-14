#include "controller_core.h"
#include "controller_indicator.h"
#include "doser.h"
#include "water_pump.h"
#include "water_sensor.h"

#include "hal/adc_types.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define SENSOR_UNIT       ADC_UNIT_1
#define SENSOR_BITWIDTH   ADC_BITWIDTH_DEFAULT
#define SENSOR_ATTEN      ADC_ATTEN_DB_12
#define SENSOR_CHANNEL    ((adc_channel_t)CONFIG_WATER_LEVEL_SENSOR_ADC_CHANNEL)

static const char *TAG = "water_level";

void app_main(void)
{
    ESP_LOGI(TAG, "Booting controller");

    const water_sensor_config_t sensor_cfg = {
        .unit = SENSOR_UNIT,
        .channel = SENSOR_CHANNEL,
        .bitwidth = SENSOR_BITWIDTH,
        .atten = SENSOR_ATTEN,
        .sample_count = CONFIG_WATER_LEVEL_SENSOR_SAMPLE_COUNT,
        .poll_period_ms = CONFIG_WATER_LEVEL_POLL_PERIOD_MS,
        .threshold_mv = CONFIG_WATER_LEVEL_SENSOR_THRESHOLD_MV,
        .disconnect_mv = CONFIG_WATER_LEVEL_SENSOR_DISCONNECT_MV,
    #if CONFIG_WATER_LEVEL_SENSOR_DIGITAL_INPUT
        .use_digital_input = true,
    #else
        .use_digital_input = false,
    #endif
    #if CONFIG_WATER_LEVEL_SENSOR_DIGITAL_ACTIVE_HIGH
        .digital_active_high = true,
    #else
        .digital_active_high = false,
    #endif
    #if CONFIG_WATER_LEVEL_SENSOR_DIGITAL_PULL_UP
        .digital_pullup_en = true,
    #else
        .digital_pullup_en = false,
    #endif
    #if CONFIG_WATER_LEVEL_SENSOR_DIGITAL_PULL_DOWN
        .digital_pulldown_en = true,
    #else
        .digital_pulldown_en = false,
    #endif
    };
    ESP_ERROR_CHECK(water_sensor_start(&sensor_cfg));

#if CONFIG_WATER_LEVEL_LED_GPIO >= 0
    const controller_indicator_config_t indicator_cfg = {
        .data_gpio = (gpio_num_t)CONFIG_WATER_LEVEL_LED_GPIO,
        .pixel_count = 1,
        .brightness = CONFIG_WATER_LEVEL_LED_BRIGHTNESS,
    };
    ESP_ERROR_CHECK(controller_indicator_start(&indicator_cfg));
#endif

    const water_pump_config_t pump_cfg = {
        .relay_gpio = (gpio_num_t)CONFIG_WATER_LEVEL_RELAY_GPIO,
    };
    ESP_ERROR_CHECK(water_pump_start(&pump_cfg));

    const doser_config_t doser_cfg = {
        .relay_gpios = {
            (gpio_num_t)CONFIG_DOSER1_GPIO,
            (gpio_num_t)CONFIG_DOSER2_GPIO,
        },
        .ms_per_ml = CONFIG_DOSER_MS_PER_ML,
    };
    ESP_ERROR_CHECK(doser_start(&doser_cfg));

    ESP_ERROR_CHECK(controller_core_init());
}
