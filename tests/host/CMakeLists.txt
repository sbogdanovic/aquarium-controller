cmake_minimum_required(VERSION 3.16)

project(aquarium_host_tests C)

enable_testing()

add_executable(wifi_bootstrap_config_tests
    ../../components/wifi_bootstrap/wifi_bootstrap_config.c
    ../../components/wifi_bootstrap/tests/test_wifi_bootstrap_config.c
)

target_include_directories(wifi_bootstrap_config_tests
    PRIVATE
        ../../components/wifi_bootstrap/include
)

if(MSVC)
    target_compile_options(wifi_bootstrap_config_tests PRIVATE /W4)
else()
    target_compile_options(wifi_bootstrap_config_tests PRIVATE -Wall -Wextra)
endif()

add_test(
    NAME wifi_bootstrap_config_tests
    COMMAND wifi_bootstrap_config_tests
)
