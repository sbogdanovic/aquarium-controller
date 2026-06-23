#include "wifi_bootstrap_config.h"

#include <string.h>

wifi_bootstrap_config_status_t wifi_bootstrap_validate_config(const char *ssid,
                                                              const char *password)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        return WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED;
    }

    if (password == NULL || strlen(password) == 0) {
        return WIFI_BOOTSTRAP_CONFIG_STATUS_OPEN_NETWORK;
    }

    return WIFI_BOOTSTRAP_CONFIG_STATUS_SECURED_NETWORK;
}

bool wifi_bootstrap_should_attempt_connect(wifi_bootstrap_config_status_t status)
{
    return status != WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED;
}
