# ESP32-S3 Water Level Controller

Firmware built with ESP-IDF for monitoring an XKC-Y23A water level sensor on an ESP32-S3. If the measured level drops below the configured threshold, a relay output is asserted and an addressable RGB LED provides visual feedback.

## Features
- Dedicated sensor task that averages ADC samples (or majority-votes a digital GPIO) from the XKC-Y23A probe and posts LOW/OK/UNKNOWN events onto its own queue
- Core state-machine task that consumes the sensor queue and issues pump/indicator commands
- Water-pump actuator task with an internal queue; `water_pump_publish()` lets the controller request ENABLE/DISABLE/FAULT actions without sharing state
- Dedicated RGB indicator task that accepts `controller_indicator_publish()` commands and maps them to WS2812 colors (defaults to the ESP32-S3 on-board LED)
- Two independent dosing channels that expose `doser_publish()` so other workflows can enqueue milliliter requests per relay
- Simple configuration via `menuconfig` (GPIO selection, threshold, sampling rate, disconnect guard, dosing parameters)
- Optional station-mode Wi-Fi bootstrap with developer-provided SSID/password and bounded startup connect window

## Architecture Overview
- **water_sensor component** polls the ADC, classifies the reading, and writes `water_sensor_event_t` values to its queue; `water_sensor_subscribe()` hands that queue to any consumer.
- **controller_core component** waits on the sensor queue, keeps the system state, and calls the actuator publish functions (`water_pump_publish()`, `controller_indicator_publish()`).
- **water_pump component** exposes a thread-safe `water_pump_publish()` API that pushes commands into its internal queue before driving the relay.
- **controller_indicator component** exposes `controller_indicator_publish()` so the controller can request LOW/OK/FAULT colors without touching LED state directly.
- **doser component** provides `doser_publish()` that enqueues per-channel milliliter jobs and keeps each relay on for `ml * ms_per_ml` to meter additives precisely.
- All reusable pieces live under `components/`, so adding new sensors or actuators means defining their events/commands in the local header and wiring queues appropriately.

## Hardware Assumptions
- ESP32-S3 DevKit with an on-board RGB LED (defaults to GPIO38 on the Wemos/LOLIN S3; adjust if your board wires the LED elsewhere)
- Relay (or MOSFET driver) controlled through GPIO 5
- XKC-Y23A output wired to ADC1 channel 0 / GPIO0 (analog mode) or the same pad treated as a digital input when *Water level sensor ▸ Use digital-output water level sensor* is enabled
- Two additive dosing relays (or MOSFETs) on GPIO7 and GPIO15 (disable any channel by setting its GPIO to -1)

Adjust GPIO numbers in `idf.py menuconfig` if your board uses different pins.

## Getting Started
1. Install ESP-IDF v6.0 or newer and export the environment.
2. Set the target once in the project directory:
   ```sh
   idf.py set-target esp32s3
   ```
3. Build and flash (replace `/dev/tty.usbmodemXYZ` with your serial port):
   ```sh
   idf.py -p /dev/tty.usbmodemXYZ flash monitor
   ```
4. Watch the console for sensor voltage logs and ensure the LED/relay tracks the water level.

### Clean build (when switching IDF versions or after major changes)

```sh
rm -rf build
idf.py set-target esp32s3
idf.py build
```

## Host Unit Tests

Run all host tests from the repository root.

Preferred (one command):

```sh
./scripts/run-host-tests.sh
```

Manual CMake/CTest flow:

```sh
cmake -S tests/host -B build-host-tests
cmake --build build-host-tests --parallel
cd build-host-tests && ctest --output-on-failure
```

Direct compile fallback (Wi-Fi bootstrap config test only):

```sh
cc -std=c11 -Wall -Wextra -Icomponents/wifi_bootstrap/include \
   components/wifi_bootstrap/wifi_bootstrap_config.c \
   components/wifi_bootstrap/tests/test_wifi_bootstrap_config.c \
   -o /tmp/wifi_bootstrap_config_tests && /tmp/wifi_bootstrap_config_tests
```

For component-specific notes (including ESP-IDF Unity on-device tests), see `components/wifi_bootstrap/README.md`.

## Configuration Tips
- Use `idf.py menuconfig` ▸ *Water Level Controller* to change GPIO assignments, switch between analog/digital sensor modes, set the digital active level/pull resistors, adjust the sample count, poll period, or the sensor-disconnect guard voltage.
- Enable `Wi-Fi Bootstrap ▸ Enable Wi-Fi bootstrap` to connect in station mode using `Wi-Fi SSID` and `Wi-Fi password`.
- `Startup connect window (ms)` bounds boot wait time for first connection. After timeout, the firmware continues locally and reconnects in the background.
- If you do not have the RGB LED wired, set `Water Level Controller ▸ On-board LED GPIO` to `-1` to disable the indicator task. For the Wemos/LOLIN S3 boards the built-in RGB LED lives on GPIO38.
- Configure the *Water Level Controller ▸ Doser* options to remap relay GPIOs or tweak the ms/ml conversion. Disable any channel by setting its GPIO to `-1`.
- The dry threshold (`mV`) should be determined empirically by logging the sensor voltage with and without water; keep the disconnect guard just below the minimum connected voltage.

## Repository Layout
- `main/main.c` – Application entry point that wires components together before starting the controller core
- `components/wifi_bootstrap` – Reusable Wi-Fi station bootstrap component with its own Kconfig and startup/reconnect behavior
- `components/water_level_sensor_component` – Sensor task, ADC handling, event enum, and queue subscription helper
- `components/controller_core` – State machine that monitors the sensor queue and drives actuators through their publish APIs
- `components/water_pump` – Actuator task with a private queue and `water_pump_publish()` helper for relay control
- `components/controller_indicator` – RGB LED task that maps indicator commands to a WS2812 LED via RMT
- `components/doser` – Multi-channel dosing manager with `doser_publish()` for enqueuing milliliter requests
- `components/button_input` – Interrupt-driven on-board button listener with its own queue and enum
- `main/Kconfig.projbuild` – Custom configuration menu
- `sdkconfig.defaults` – Default target and pin assignments

## Next Steps
- Tune the sampling interval and threshold to match your plumbing setup
- Add Wi-Fi or BLE connectivity to report the tank status remotely if needed
