#include "wifi_bootstrap_config.h"

#include <string.h>

// ESP-IDF wifi_sta_config_t fields are sized as ssid[32] and password[64].
// For null-terminated C strings, this means max lengths of 31 and 63 respectively.
#define WIFI_BOOTSTRAP_SSID_MAX_LEN     32
#define WIFI_BOOTSTRAP_PASSWORD_MAX_LEN 64

wifi_bootstrap_config_status_t wifi_bootstrap_validate_config(const char *ssid,
                                                              const char *password)
{
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) >= WIFI_BOOTSTRAP_SSID_MAX_LEN) {
        return WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED;
    }

    if (password == NULL || password[0] == '\0') {
        return WIFI_BOOTSTRAP_CONFIG_STATUS_OPEN_NETWORK;
    }

    if (strlen(password) >= WIFI_BOOTSTRAP_PASSWORD_MAX_LEN) {
        return WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED;
    }

    return WIFI_BOOTSTRAP_CONFIG_STATUS_SECURED_NETWORK;
}

bool wifi_bootstrap_should_attempt_connect(wifi_bootstrap_config_status_t status)
{
    return status != WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED;
}
