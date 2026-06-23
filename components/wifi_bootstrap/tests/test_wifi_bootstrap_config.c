#include <assert.h>
#include <stdio.h>

#include "wifi_bootstrap_config.h"

static void test_missing_ssid_is_misconfigured(void)
{
    const wifi_bootstrap_config_status_t status = wifi_bootstrap_validate_config("", "secret");
    assert(status == WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED);
    assert(!wifi_bootstrap_should_attempt_connect(status));
}

static void test_null_ssid_is_misconfigured(void)
{
    const wifi_bootstrap_config_status_t status = wifi_bootstrap_validate_config(NULL, "secret");
    assert(status == WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED);
    assert(!wifi_bootstrap_should_attempt_connect(status));
}

static void test_open_network_with_empty_password(void)
{
    const wifi_bootstrap_config_status_t status = wifi_bootstrap_validate_config("lab-net", "");
    assert(status == WIFI_BOOTSTRAP_CONFIG_STATUS_OPEN_NETWORK);
    assert(wifi_bootstrap_should_attempt_connect(status));
}

static void test_open_network_with_null_password(void)
{
    const wifi_bootstrap_config_status_t status = wifi_bootstrap_validate_config("lab-net", NULL);
    assert(status == WIFI_BOOTSTRAP_CONFIG_STATUS_OPEN_NETWORK);
    assert(wifi_bootstrap_should_attempt_connect(status));
}

static void test_secured_network_with_password(void)
{
    const wifi_bootstrap_config_status_t status =
        wifi_bootstrap_validate_config("lab-net", "topsecret");
    assert(status == WIFI_BOOTSTRAP_CONFIG_STATUS_SECURED_NETWORK);
    assert(wifi_bootstrap_should_attempt_connect(status));
}

static void test_too_long_ssid_is_misconfigured(void)
{
    char ssid[33];
    for (int i = 0; i < 32; i++) {
        ssid[i] = 'a';
    }
    ssid[32] = '\0';

    const wifi_bootstrap_config_status_t status = wifi_bootstrap_validate_config(ssid, "secret");
    assert(status == WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED);
    assert(!wifi_bootstrap_should_attempt_connect(status));
}

static void test_too_long_password_is_misconfigured(void)
{
    char password[65];
    for (int i = 0; i < 64; i++) {
        password[i] = 'b';
    }
    password[64] = '\0';

    const wifi_bootstrap_config_status_t status =
        wifi_bootstrap_validate_config("lab-net", password);
    assert(status == WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED);
    assert(!wifi_bootstrap_should_attempt_connect(status));
}

int main(void)
{
    test_missing_ssid_is_misconfigured();
    test_null_ssid_is_misconfigured();
    test_open_network_with_empty_password();
    test_open_network_with_null_password();
    test_secured_network_with_password();
    test_too_long_ssid_is_misconfigured();
    test_too_long_password_is_misconfigured();

    puts("wifi_bootstrap_config tests passed");
    return 0;
}
