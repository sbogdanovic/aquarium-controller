set(wifi_bootstrap_srcs
    "wifi_bootstrap.c"
    "wifi_bootstrap_config.c"
)

if(CONFIG_UNITY_ENABLE_IDF_TEST_RUNNER)
    list(APPEND wifi_bootstrap_srcs "wifi_bootstrap_test_unity.c")
endif()

idf_component_register(
    SRCS ${wifi_bootstrap_srcs}
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif nvs_flash
    PRIV_REQUIRES unity
)
