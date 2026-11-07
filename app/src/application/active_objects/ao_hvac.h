/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_hvac.h
 * @brief Active Object coordinator for Industrial HVAC Subsystem.
 *
 * Coordinates group aggregation, unit rotation, anti-inrush delays,
 * alarm lockout enforcement, and periodic status snapshot broadcasting.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_HVAC_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_HVAC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "application/zbus/hvac_channels.h"
#include "application/zbus/sensor_channels.h"
#include "common/hvac_types.h"
#include "domain/models/hvac_group_entity.h"
#include "domain/models/hvac_unit_entity.h"
#include "interfaces/i_hvac_unit.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Thread stack size reserved for the HVAC Active Object. */
#define AO_HVAC_STACK_SIZE 4096

/** Priority for the HVAC Active Object thread. */
#define AO_HVAC_PRIORITY 5

/** Queue depth for incoming control events. */
#define AO_HVAC_QUEUE_DEPTH 16

/** Default periodic evaluation period: 1000 ms = 1 Hz. */
#define AO_HVAC_PERIOD_MS 1000U

/** Timeout to wait for the Active Object thread to exit on stop. */
#define AO_HVAC_STOP_WAIT_MS 1000U

/**
 * @brief Command event types for ao_hvac queue.
 */
typedef enum {
    AO_HVAC_EVENT_STEP_NOW = 0, /**< Trigger an immediate evaluation step */
    AO_HVAC_EVENT_STOP          /**< Terminate Active Object inner loop */
} ao_hvac_event_type_t;

/**
 * @brief Event message structure for ao_hvac queue.
 */
typedef struct {
    ao_hvac_event_type_t type;
} ao_hvac_event_t;

/* Forward declaration of actuator adapter */
struct adapter_hvac_actuator_s;

/**
 * @brief Active Object HVAC coordinator context.
 */
typedef struct {
    /* Actuator port handle */
    i_hvac_unit_t actuator_port;

    /* Optional actuator adapter for runtime hardware routing updates */
    struct adapter_hvac_actuator_s *actuator_adapter;

    /* Configuration copy */
    config_hvac_t config;

    /* Managed Domain Entities */
    hvac_unit_entity_t  units[HVAC_MAX_UNITS_TOTAL];
    hvac_group_entity_t groups[HVAC_MAX_GROUPS];
    bool                group_active[HVAC_MAX_GROUPS];
    bool                unit_active[HVAC_MAX_UNITS_TOTAL];

    /* Runtime telemetry & inputs */
    group_temperature_metrics_t group_metrics[HVAC_MAX_GROUPS];
    bool                        is_fire_active;
    sensor_snapshot_msg_t       last_sensor_snapshot;
    uint8_t                     boot_stabilization_s;

    /* Synchronization and Thread Control */
    struct k_msgq    *queue;
    struct k_thread   thread;
    k_thread_stack_t *stack;
    size_t            stack_size;
    struct k_sem      run_sem;
    struct k_sem      stopped_sem;
    struct k_mutex    lock;
    volatile bool     is_running;
    volatile bool     terminate;
    bool              is_initialized;
    uint32_t          actuator_fault_count; /**< Counter of failed actuation attempts */

    /* Previous states for transition event generation */
    hvac_group_state_t prev_group_state[HVAC_MAX_GROUPS];
    hvac_unit_state_t  prev_unit_state[HVAC_MAX_UNITS_TOTAL];
    uint8_t            prev_turn_offset[HVAC_MAX_GROUPS];
    bool               prev_manual_active[HVAC_MAX_GROUPS];
} ao_hvac_t;

/**
 * @brief Force all HVAC units to a safe de-energized state (all relays OFF).
 *
 * @param[in,out] self Active Object instance.
 * @return 0 on success, negative errno on failure.
 */
int ao_hvac_safe_state(ao_hvac_t *self);

/**
 * @brief Phase 1: Initialize the HVAC Active Object.
 *
 * Spawns the worker thread once in gated state waiting on run_sem.
 *
 * @param[out] self          Instance to initialize.
 * @param[in]  config        Initial HVAC configuration (may be NULL for default 8-unit setup).
 * @param[in]  actuator_port Bound handle to i_hvac_unit_t actuator port.
 * @param[in]  queue         Pre-allocated command message queue.
 * @param[in]  stack         File-scope thread stack buffer.
 * @param[in]  stack_size    Size of thread stack in bytes.
 * @return 0 on success, negative errno on failure.
 */
int ao_hvac_init(ao_hvac_t *self,
                 const config_hvac_t *config,
                 const i_hvac_unit_t *actuator_port,
                 struct k_msgq *queue,
                 k_thread_stack_t *stack,
                 size_t stack_size);

/**
 * @brief Phase 2: Start the HVAC Active Object execution thread.
 *
 * Unblocks run_sem without recreating the thread.
 *
 * @param[in,out] self Initialized Active Object instance.
 * @return 0 on success, -EALREADY if already running, -EINVAL on invalid state.
 */
int ao_hvac_start(ao_hvac_t *self);

/**
 * @brief Phase 3: Execute one synchronous evaluation step.
 *
 * Callable directly in unit tests without a running thread.
 * Evaluates active groups, advances 1-second timers, updates tri-state actuators,
 * and publishes status to chan_hvac_status.
 *
 * @param[in,out] self Active Object instance.
 * @return 0 on success, negative errno on failure.
 */
int ao_hvac_step(ao_hvac_t *self);

/**
 * @brief Phase 4: Stop the HVAC Active Object gracefully.
 *
 * Signals stop, forces safe state, and unblocks loop.
 * Does not block waiting on stopped_sem (use ao_hvac_wait_stopped).
 *
 * @param[in,out] self Active Object instance.
 * @return 0 on success, negative errno on error.
 */
int ao_hvac_stop(ao_hvac_t *self);

/**
 * @brief Wait until the HVAC Active Object confirms it has stopped.
 *
 * @param[in,out] self    Active Object instance.
 * @param[in]     timeout Maximum time to wait.
 * @return 0 on success, -EAGAIN on timeout, negative errno on error.
 */
int ao_hvac_wait_stopped(ao_hvac_t *self, k_timeout_t timeout);

/**
 * @brief Phase 5: Deinitialize the HVAC Active Object and terminate worker thread.
 *
 * @param[in,out] self Active Object instance in stopped state.
 * @return 0 on success, -EBUSY if running, negative errno on error.
 */
int ao_hvac_deinit(ao_hvac_t *self);

/**
 * @brief Update aggregated temperature metrics for an HVAC group.
 *
 * @param[in,out] self     Active Object instance.
 * @param[in]     group_id Group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in]     metrics  Temperature metrics descriptor.
 * @return 0 on success, negative errno on invalid arguments.
 */
int ao_hvac_set_group_metrics(ao_hvac_t *self,
                              uint8_t group_id,
                              const group_temperature_metrics_t *metrics);

/**
 * @brief Feed a multi-sensor snapshot into the HVAC Active Object.
 *
 * Ingests a wide temperature array with 64-bit valid_mask (for flat sensor IDs 1..59).
 * Evaluates each active group:
 * - If sensor_start > 0 and sensor_end >= sensor_start, computes metrics using
 *   hvac_compute_group_metrics_range().
 * - Otherwise, falls back to legacy sensor_mask using hvac_compute_group_metrics().
 *
 * @param[in,out] self         Active Object instance.
 * @param[in]     temperatures Array of temperature readings in °C.
 * @param[in]     valid_mask   64-bit mask of healthy sensor channels (bit s = slot s valid).
 * @param[in]     count        Number of entries in @p temperatures.
 * @return 0 on success, negative errno on invalid arguments.
 */
int ao_hvac_feed_sensor_snapshot(ao_hvac_t *self,
                                 const float *temperatures,
                                 uint64_t valid_mask,
                                 size_t count);

/**
 * @brief Feed fresh temperature readings into the HVAC Active Object.
 *
 * Ingests temperature readings regardless of physical origin (on-board ADC RTD or
 * external Modbus RTU). Automatically computes per-group metrics (highest, lowest,
 * average, valid count) based on each group's configured sensor_mask.
 *
 * @param[in,out] self         Active Object instance.
 * @param[in]     temperatures Array of temperature readings in °C.
 * @param[in]     valid_mask   Bitmask of healthy sensor channels.
 * @param[in]     count        Number of entries in @p temperatures.
 * @return 0 on success, negative errno on invalid arguments.
 */
int ao_hvac_feed_temperatures(ao_hvac_t *self,
                              const float *temperatures,
                              uint16_t valid_mask,
                              size_t count);

/**
 * @brief Update physical safety alarms for an individual AC unit.
 *
 * @param[in,out] self    Active Object instance.
 * @param[in]     unit_id Unit ID (0 .. HVAC_MAX_UNITS_TOTAL - 1).
 * @param[in]     hp      High pressure switch alarm.
 * @param[in]     lp      Low pressure switch alarm.
 * @param[in]     flow    Airflow / water supply fault alarm.
 * @param[in]     volt    Voltage / phase monitor fault alarm.
 * @return 0 on success, negative errno on invalid arguments.
 */
int ao_hvac_set_unit_alarms(ao_hvac_t *self,
                            uint8_t unit_id,
                            bool hp,
                            bool lp,
                            bool flow,
                            bool volt);

/**
 * @brief Set or clear emergency fire shutdown interlock across all groups.
 *
 * @param[in,out] self   Active Object instance.
 * @param[in]     active true to force fire shutdown, false to restore normal operation.
 * @return 0 on success, negative errno on invalid arguments.
 */
int ao_hvac_set_fire(ao_hvac_t *self, bool active);

/**
 * @brief Enable or disable manual override for a specific group.
 *
 * @param[in,out] self     Active Object instance.
 * @param[in]     group_id Group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in]     active   true to force manual run, false to cancel.
 * @return 0 on success, negative errno on invalid arguments.
 */
int ao_hvac_set_manual(ao_hvac_t *self, uint8_t group_id, bool active);

/**
 * @brief Set or clear manual override mode on an HVAC group (convenience alias).
 *
 * @param[in,out] self     Active object instance.
 * @param[in]     group_id Target group identifier (0 .. HVAC_MAX_GROUPS-1).
 * @param[in]     active   true to enable manual override, false to restore automatic control.
 * @return 0 on success, negative errno on invalid arguments.
 */
int ao_hvac_set_group_manual(ao_hvac_t *self, uint8_t group_id, bool active);

/**
 * @brief Set or clear a bypass cause on an HVAC group.
 *
 * @param[in,out] self     Active object instance.
 * @param[in]     group_id Target group identifier (0 .. HVAC_MAX_GROUPS-1).
 * @param[in]     cause    Bypass cause to set or clear (@see hvac_bypass_cause_t).
 * @param[in]     active   true to assert bypass cause, false to clear.
 * @return 0 on success, negative errno on invalid arguments or uninitialized AO.
 */
int ao_hvac_set_bypass_cause(ao_hvac_t *self, uint8_t group_id, hvac_bypass_cause_t cause, bool active);

/**
 * @brief Query the latest status snapshot from the Active Object.
 *
 * @param[in,out] self       Active Object instance.
 * @param[out]    out_status Destination for snapshot.
 * @return 0 on success, negative errno on invalid arguments.
 */
int ao_hvac_get_status(ao_hvac_t *self, hvac_status_msg_t *out_status);

/**
 * @brief Dynamically update configuration for a specific HVAC group at runtime.
 *
 * Updates setpoints, delay_on_s, active_turn_count, cooling_timeout_s,
 * fan_delay_s, and units' settings in RAM immediately without disturbing the FSM
 * or forcing a reboot.
 *
 * @param[in,out] self     Active Object instance.
 * @param[in]     group_id Group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in]     cfg      Group configuration parameters.
 * @return 0 on success, negative errno on failure.
 */
int ao_hvac_set_group_config(ao_hvac_t *self, uint8_t group_id, const config_hvac_group_t *cfg);

/**
 * @brief Dynamically apply a complete HVAC configuration across all groups at runtime.
 *
 * @param[in,out] self Active Object instance.
 * @param[in]     cfg  Complete HVAC configuration.
 * @return 0 on success, negative errno on failure.
 */
int ao_hvac_apply_config(ao_hvac_t *self, const config_hvac_t *cfg);

/**
 * @brief Thread-safe query of the active HVAC configuration.
 *
 * @param[in,out] self    Active Object instance.
 * @param[out]    out_cfg Destination configuration struct.
 * @return 0 on success, negative errno on failure.
 */
int ao_hvac_get_config(ao_hvac_t *self, config_hvac_t *out_cfg);

/**
 * @brief Bind actuator adapter instance to Active Object for runtime hardware routing.
 *
 * @param[in,out] self             Active Object instance.
 * @param[in]     actuator_adapter Pointer to actuator adapter instance.
 */
void ao_hvac_set_actuator_adapter(ao_hvac_t *self, struct adapter_hvac_actuator_s *actuator_adapter);

/**
 * @brief Force immediate unit duty rotation turn for an HVAC group.
 *
 * @param[in,out] self     Active Object instance.
 * @param[in]     group_id Group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @return 0 on success, negative errno on error.
 */
int ao_hvac_force_rotation(ao_hvac_t *self, uint8_t group_id);

/**
 * @brief Update duty rotation interval in days for a specific HVAC group.
 *
 * @param[in,out] self     Active Object instance.
 * @param[in]     group_id Group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in]     days     Rotation interval in days (e.g. 7, 15, 20, 30).
 * @return 0 on success, negative errno on error.
 */
int ao_hvac_set_group_rotation(ao_hvac_t *self, uint8_t group_id, uint16_t days);

/**
 * @brief Manually override compressor or fan state for an individual AC unit.
 *
 * @param[in,out] self           Active Object instance.
 * @param[in]     unit_id        Unit ID (0 .. HVAC_MAX_UNITS_TOTAL - 1).
 * @param[in]     enable         true to enable manual override, false to restore auto.
 * @param[in]     run_compressor true to run compressor.
 * @param[in]     run_fan        true to run fan purge.
 * @return 0 on success, negative errno on error.
 */
int ao_hvac_set_unit_manual(ao_hvac_t *self, uint8_t unit_id, bool enable, bool run_compressor, bool run_fan);

/**
 * @brief Check whether the Active Object execution thread is running.
 *
 * @param[in] self Active Object instance.
 * @return true if running, false otherwise.
 */
static inline bool ao_hvac_is_running(const ao_hvac_t *self)
{
    return (self != NULL && self->is_running);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_HVAC_H_ */
