#pragma once

/**
 * @file architecture.h
 * @brief Water Controller System Architecture
 *
 * ## Overview
 *
 * Every component owns its FreeRTOS queue and public API:
 *
 * - Sensors push events onto their queues and expose a `*_subscribe()` helper to share the handle.
 * - controller_core waits on the water-level sensor queue, runs the state machine, and issues
 *   actuator commands via `*_publish()` helpers.
 * - Actuators enqueue work into their private queues, so hardware manipulation stays within the
 *   component that owns the GPIO/LED logic.
 *
 * ## Data Flow
 *
 * ```
 *  water_sensor_task
 *         |
 *         v
 *  controller_core
 *         |
 *         +--> water_pump_publish()
 *         |
 *         +--> controller_indicator_publish()
 * ```
 *
 * 1. Sensors perform sampling/debounce, classify the reading (e.g., LOW/OK/UNKNOWN), and write the
 *    enum value into their queue.
 * 2. controller_core blocks on the water-level sensor queue, consumes each event, logs the
 *    transition, and decides whether to enable/disable the pump or change the indicator color.
 * 3. Actuator publish helpers enqueue commands so their tasks can touch hardware without blocking
 *    the controller_core task.
 *
 * ## Key Components
 *
 * - **water_level_sensor_component**: Defines `water_sensor_event_t`, owns the ADC driver, and
 *   exposes `water_sensor_subscribe()` plus `water_sensor_event_name()` for logging.
 * - **controller_core**: Owns the state machine and calls actuator publish helpers. It never
 *   touches hardware directly.
 * - **water_pump / controller_indicator / doser**: Each defines its own command enum/struct and a
 *   publish helper that enqueues work on a component-owned queue.
 *
 * ## Adding Sensors
 *
 * 1. Create a queue inside the sensor component (`xQueueCreate`).
 * 2. Define the public event enum and queue length constant in the component header.
 * 3. Expose `sensor_name_subscribe(QueueHandle_t *queue)` so controller_core can receive events.
 * 4. Push enum values into the queue whenever the hardware state changes.
 *
 * ## Adding Actuators
 *
 * 1. Define a command enum or struct plus a publish helper in the component header.
 * 2. Create a queue inside the actuator component and have the worker task block on it.
 * 3. Implement the publish helper so it enqueues a command (returning `ESP_ERR_INVALID_STATE` if the
 *    component is disabled).
 * 4. Have controller_core call the publish helper whenever a state transition requires it.
 *
 * ## Thread Safety
 *
 * - Sensors should only write to their queues from task context (ISRs queue into a small ISR queue
 *   and the task performs debounce before publishing major events).
 * - controller_core never blocks on actuator work; it simply writes to their queues.
 * - Actuators own their GPIO/RMT resources so there is no concurrent access from the controller.
 */

#endif // ARCHITECTURE_H
