# Component-Level Queues

## Overview

Each hardware block owns its concurrency boundaries:

- **Sensors** push strongly typed events onto their own FreeRTOS queues and expose a `*_subscribe()` helper that returns the queue handle.
- **Controller core** waits on the water-level sensor queue, runs the state machine, and issues actuator commands through dedicated publish helpers.
- **Actuators** (pump, indicator, doser) expose `*_publish()` APIs that simply enqueue work onto their private queues before touching any hardware. They define their own command enums/structs in their public headers.

This keeps all ISR/debounce/ADC logic co-located with the component that owns it, while the controller core remains the single task that translates system state into actions.

## Event Flow

```
Water Sensor Task
    |
    v
controller_core
    |
    +--> water_pump_publish()
    |
    +--> controller_indicator_publish()
    |
    v
Actuator Queues
    |
    v
Hardware
```

1. The water-level sensor component samples hardware, classifies the reading, and writes a `water_sensor_event_t` value into its queue.
2. `controller_core` blocks on that queue, dequeues the strongly typed event, and updates the state machine.
3. Based on the new state, the controller calls `water_pump_publish()` or `controller_indicator_publish()` to enqueue actuator commands without touching their internal state.
4. Each actuator task wakes up on its own queue, drives its GPIOs/LEDs, and logs the action.

## Key Components

### water_level_sensor_component
- Defines `water_sensor_event_t` and `WATER_LEVEL_SENSOR_EVENT_QUEUE_LEN`.
- Owns the ADC unit, averages samples, and enqueues LOW/OK/UNKNOWN events.
- Provides `water_sensor_subscribe()` so consumers can obtain the queue handle.

### controller_core
- Subscribes to the water-level sensor queue.
- Runs the water-level state machine, tracks controller state transitions, and logs them.
- Calls actuator publish helpers instead of broadcasting generic events.

### water_pump / controller_indicator / doser
- Each component defines the commands it understands (enums or structs) in its header.
- `*_publish()` functions enqueue commands so the task context performs the hardware operation.
- Logging occurs inside each component, keeping controller_core free of hardware details.

## Benefits

- **Clarity** – Events and commands live beside the component that produces or consumes them, making the header the single source of truth.
- **Queue Ownership** – Each task owns its queue depth/behavior; there are no hidden dispatcher tasks in a shared module.
- **Deterministic Controller** – Only `controller_core` mutates system state, and it never blocks on actuator work because it just pushes to queues.
- **Extensibility** – Adding a new sensor or actuator is as simple as defining its events/commands in its header, creating a queue, and wiring it into `controller_core`.

## Sample Usage

```c
// water_pump.h
typedef enum {
    WATER_PUMP_COMMAND_ENABLE,
    WATER_PUMP_COMMAND_DISABLE,
    WATER_PUMP_COMMAND_SENSOR_FAULT,
} water_pump_command_t;

esp_err_t water_pump_publish(water_pump_command_t command);

// controller_core.c
static void publish_pump_command(water_pump_command_t command)
{
    esp_err_t err = water_pump_publish(command);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Pump publish failed for %s (%s)",
                 water_pump_command_name(command), esp_err_to_name(err));
    }
}
```

## Controller Task Responsibilities

1. Subscribe to the water-level sensor queue.
2. Block on that queue, process sensor events immediately, and emit actuator commands.
3. Keep the high-level state machine transitions logged so behavior is traceable.

## Actuator Responsibilities

1. Own their hardware configuration and tasks.
2. Expose a publish helper that enqueues commands and returns `ESP_ERR_INVALID_STATE` if the component is disabled.
3. Log queue drops so back-pressure is visible when controller commands arrive faster than hardware can execute.

## Adding New Components

1. Create a new component with its own `include/` folder.
2. Define the public event/command enum or struct next to the `*_start()` prototype.
3. Create a FreeRTOS queue sized appropriately for the component.
4. Expose `*_subscribe()` (for sensors) or `*_publish()` (for actuators).
5. Have `controller_core` subscribe/publish accordingly.

This pattern keeps the codebase modular without the complexity of shared dispatcher tasks.
