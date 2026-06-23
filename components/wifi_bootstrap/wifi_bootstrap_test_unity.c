#include "unity.h"

#include "wifi_bootstrap_config.h"

TEST_CASE("wifi bootstrap config rejects missing ssid", "[wifi_bootstrap]")
{
    const wifi_bootstrap_config_status_t status = wifi_bootstrap_validate_config("", "secret");

    TEST_ASSERT_EQUAL(WIFI_BOOTSTRAP_CONFIG_STATUS_DISABLED_MISCONFIGURED, status);
    TEST_ASSERT_FALSE(wifi_bootstrap_should_attempt_connect(status));
}

TEST_CASE("wifi bootstrap config accepts open network", "[wifi_bootstrap]")
{
    const wifi_bootstrap_config_status_t status = wifi_bootstrap_validate_config("lab-net", "");

    TEST_ASSERT_EQUAL(WIFI_BOOTSTRAP_CONFIG_STATUS_OPEN_NETWORK, status);
    TEST_ASSERT_TRUE(wifi_bootstrap_should_attempt_connect(status));
}

TEST_CASE("wifi bootstrap config accepts secured network", "[wifi_bootstrap]")
{
    const wifi_bootstrap_config_status_t status =
        wifi_bootstrap_validate_config("lab-net", "topsecret");

    TEST_ASSERT_EQUAL(WIFI_BOOTSTRAP_CONFIG_STATUS_SECURED_NETWORK, status);
    TEST_ASSERT_TRUE(wifi_bootstrap_should_attempt_connect(status));
}
