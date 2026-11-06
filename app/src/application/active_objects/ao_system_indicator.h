/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_system_indicator.h
 * @brief Active Object coordinating Status LED and Buzzer indicators.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_SYSTEM_INDICATOR_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_SYSTEM_INDICATOR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "common/indicator_types.h"
#include "interfaces/i_buzzer.h"
#include "interfaces/i_led.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AO_INDICATOR_STACK_SIZE 1536
#define AO_INDICATOR_QUEUE_DEPTH 16
#define AO_INDICATOR_PRIORITY 8
#define AO_INDICATOR_TICK_MS 25U
#define AO_INDICATOR_MAX_ALARMS 16

/**
 * @brief Event types received by ao_system_indicator message queue.
 */
typedef enum {
    AO_INDICATOR_EVT_START = 1,     /**< Start inner processing loop */
    AO_INDICATOR_EVT_STOP,          /**< Request to stop Active Object thread */
    AO_INDICATOR_EVT_TERMINATE,     /**< Terminate thread permanently */
    AO_INDICATOR_EVT_ALARM,         /**< Alarm report or clearance */
    AO_INDICATOR_EVT_HEALTH,        /**< System health update */
    AO_INDICATOR_EVT_COMMAND,       /**< Manual override command */
    AO_INDICATOR_EVT_STARTUP_CHIME, /**< Request to run startup chime */
} ao_indicator_event_type_t;

/**
 * @brief Event message structure for ao_system_indicator queue.
 */
typedef struct {
    ao_indicator_event_type_t type;
    union {
        alarm_msg_t         alarm;
        system_health_msg_t health;
        indicator_cmd_msg_t cmd;
    } data;
} ao_indicator_event_t;

/**
 * @brief Internal entry tracking active alarms.
 */
typedef struct {
    uint16_t      alarm_id;
    alarm_level_t level;
    bool          is_active;
} ao_indicator_alarm_entry_t;

/**
 * @brief System Indicator Active Object state.
 */
typedef struct {
    /* Injected ports */
    const i_led_t    *led;
    const i_buzzer_t *buzzer;

    /* RTOS Threading and Queue */
    struct k_msgq     *queue;
    struct k_thread    thread;
    k_thread_stack_t  *stack;
    size_t             stack_size;
    volatile bool      is_running;

    /* Synchronization */
    struct k_sem       stopped_sem;
    bool               is_initialized;

    /* Priority State Machine Data */
    ao_indicator_alarm_entry_t alarms[AO_INDICATOR_MAX_ALARMS];
    alarm_level_t              highest_alarm_level;
    system_health_t            current_health;

    /* Manual Override State */
    bool                       manual_override;
    indicator_cmd_t            manual_cmd;
    led_state_t                manual_led_state;
    buzzer_pattern_t           manual_buzzer_pattern;
    uint32_t                   manual_duration_ms;
    uint32_t                   manual_start_ms;

    /* Startup Chime State */
    bool                       startup_chime_active;
    uint32_t                   chime_start_ms;

    /* One-shot Beep State */
    uint32_t                   beep_start_ms;
    uint32_t                   beep_duration_ms;
    bool                       beep_active;

    /* Current Physical Hardware State (to avoid redundant driver writes) */
    led_state_t                target_led_state;
    buzzer_pattern_t           target_buzzer_pattern;
    bool                       last_hw_led_on;
    bool                       last_hw_buzzer_on;
    uint32_t                   last_hw_buzzer_freq;
} ao_system_indicator_t;

/**
 * @brief Initialize the System Indicator Active Object.
 *
 * @param[out] self        Instance to initialize.
 * @param[in]  led         Bound LED port handle.
 * @param[in]  buzzer      Bound Buzzer port handle.
 * @param[in]  queue       Message queue.
 * @param[in]  stack       Thread stack defined at file scope.
 * @param[in]  stack_size  Size of the stack buffer.
 * @return 0 on success, negative errno on failure.
 */
int ao_system_indicator_init(ao_system_indicator_t *self,
                             const i_led_t *led,
                             const i_buzzer_t *buzzer,
                             struct k_msgq *queue,
                             k_thread_stack_t *stack,
                             size_t stack_size);

/**
 * @brief Start the System Indicator Active Object thread.
 */
int ao_system_indicator_start(ao_system_indicator_t *self);

/**
 * @brief Stop the System Indicator Active Object thread.
 */
int ao_system_indicator_stop(ao_system_indicator_t *self);

/**
 * @brief Wait for the Active Object thread to exit its inner work loop.
 *
 * @param[in] self     Active object handle.
 * @param[in] timeout  Maximum duration to wait.
 * @return 0 on success, -EAGAIN on timeout, -EINVAL on invalid handle.
 */
int ao_system_indicator_wait_stopped(ao_system_indicator_t *self, k_timeout_t timeout);

/**
 * @brief Deinitialize the Active Object and terminate its thread.
 *
 * @param[in] self  Active object handle.
 * @return 0 on success, negative errno on failure.
 */
int ao_system_indicator_deinit(ao_system_indicator_t *self);

/**
 * @brief Post an alarm event to the indicator queue.
 */
int ao_system_indicator_post_alarm(ao_system_indicator_t *self, const alarm_msg_t *msg);

/**
 * @brief Post a system health event to the indicator queue.
 */
int ao_system_indicator_post_health(ao_system_indicator_t *self, const system_health_msg_t *msg);

/**
 * @brief Post a manual override command to the indicator queue.
 */
int ao_system_indicator_post_command(ao_system_indicator_t *self, const indicator_cmd_msg_t *cmd);

/**
 * @brief Request the smart industrial startup chime sequence.
 */
int ao_system_indicator_trigger_startup_chime(ao_system_indicator_t *self);

/**
 * @brief Query the current status of indicators.
 */
int ao_system_indicator_get_status(const ao_system_indicator_t *self, indicator_status_t *out_status);

/**
 * @brief Process an event synchronously (exposed for deterministic testing).
 */
int ao_system_indicator_process_event(ao_system_indicator_t *self, const ao_indicator_event_t *evt);

/**
 * @brief Advance state machine and software timing by one tick (exposed for testing).
 *
 * @param[in,out] self    Indicator active object instance.
 * @param[in]     now_ms  Current timestamp in milliseconds.
 * @return 0 on success, negative errno on failure.
 */
int ao_system_indicator_step(ao_system_indicator_t *self, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_SYSTEM_INDICATOR_H_ */
