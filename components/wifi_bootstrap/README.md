# wifi_bootstrap Component

Reusable ESP-IDF component that provides station-mode Wi-Fi bootstrap with:

- developer-configured SSID/password
- bounded startup connect wait window
- background reconnect after disconnect
- non-fatal startup behavior for local control loops

## Configuration

Open `idf.py menuconfig` and configure:

- `Wi-Fi Bootstrap -> Enable Wi-Fi bootstrap`
- `Wi-Fi Bootstrap -> Wi-Fi SSID`
- `Wi-Fi Bootstrap -> Wi-Fi password`
- `Wi-Fi Bootstrap -> Startup connect window (ms)`

## Plug Into Application

1. Add dependency in your component CMake:

```cmake
idf_component_register(
    SRCS "main.c"
    REQUIRES wifi_bootstrap
)
```

2. Call bootstrap from your startup path:

```c
#include "wifi_bootstrap.h"

const esp_err_t wifi_err = wifi_bootstrap_start();
if (wifi_err != ESP_OK) {
    // Keep local control active even if Wi-Fi setup fails.
}
```

## Unit Tests

This component includes host-runnable unit tests for config validation logic in:

- `tests/test_wifi_bootstrap_config.c`

Preferred from repository root:

```sh
scripts/run-host-tests.sh
```

Manual CMake/CTest flow:

```sh
cmake -S tests/host -B build-host-tests
cmake --build build-host-tests
cd build-host-tests && ctest --output-on-failure
```

Direct compile fallback:

```sh
cc -std=c11 -Wall -Wextra -Icomponents/wifi_bootstrap/include \
  components/wifi_bootstrap/wifi_bootstrap_config.c \
  components/wifi_bootstrap/tests/test_wifi_bootstrap_config.c \
  -o /tmp/wifi_bootstrap_config_tests && /tmp/wifi_bootstrap_config_tests
```

## ESP-IDF Unity Tests (On Device)

The component also exposes Unity test cases compiled into the firmware when
`CONFIG_UNITY_ENABLE_IDF_TEST_RUNNER` is enabled (default in this project).

Test source:

- `wifi_bootstrap_test_unity.c`

Build and run with IDF:

```sh
idf.py build
idf.py -p /dev/tty.usbmodemXYZ flash monitor
```

When the Unity prompt appears in monitor, run:

- `*` to run all tests
- `[wifi_bootstrap]` to run only Wi-Fi bootstrap tagged tests
