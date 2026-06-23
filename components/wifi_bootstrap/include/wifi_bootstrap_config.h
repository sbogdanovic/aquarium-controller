#pragma once

#include <stdbool.h>

typedef enum {
    WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED,
    WIFI_BOOTSTRAP_CONFIG_STATUS_OPEN_NETWORK,
    WIFI_BOOTSTRAP_CONFIG_STATUS_SECURED_NETWORK,
} wifi_bootstrap_config_status_t;

wifi_bootstrap_config_status_t wifi_bootstrap_validate_config(const char *ssid,
                                                              const char *password);

bool wifi_bootstrap_should_attempt_connect(wifi_bootstrap_config_status_t status);
