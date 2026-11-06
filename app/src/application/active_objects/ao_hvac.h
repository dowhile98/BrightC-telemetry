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

/**
 * @brief Active Object HVAC coordinator context.
 */
typedef struct {
    /* Actuator port handle */
    i_hvac_unit_t actuator_port;

    /* Managed Domain Entities */
    hvac_unit_entity_t  units[HVAC_MAX_UNITS_TOTAL];
    hvac_group_entity_t groups[HVAC_MAX_GROUPS];
    bool                group_active[HVAC_MAX_GROUPS];
    bool                unit_active[HVAC_MAX_UNITS_TOTAL];

    /* Runtime telemetry & inputs */
    group_temperature_metrics_t group_metrics[HVAC_MAX_GROUPS];
    bool                        is_fire_active;

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
} ao_hvac_t;

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
 * Signals stop, unblocks loop, and waits for confirmation via stopped_sem.
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
