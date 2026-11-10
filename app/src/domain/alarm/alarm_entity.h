/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_entity.h
 * @brief Pure Domain Entity: Single-Shot Edge Triggered Alarm State Machine.
 *
 * ISO C99 only. Zero Zephyr RTOS or vendor hardware dependencies.
 *
 * State Machine Transitions:
 *   INACTIVE  -> ACTIVE       (Condition asserted, ON_PRESS)
 *   ACTIVE    -> INACTIVE     (Condition cleared, Latch disabled, ON_RELEASE)
 *   ACTIVE    -> LATCHED      (Condition cleared, Latch enabled)
 *   ACTIVE    -> ACKNOWLEDGED (Operator acknowledges active condition)
 *   ACKNOWLEDGED -> INACTIVE (Condition cleared, Latch disabled, ON_RELEASE)
 *   ACKNOWLEDGED -> LATCHED  (Condition cleared, Latch enabled)
 *   LATCHED   -> INACTIVE    (Operator reset command, ON_RELEASE)
 */

#ifndef APP_DOMAIN_ALARM_ENTITY_H_
#define APP_DOMAIN_ALARM_ENTITY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/alarm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Discrete Alarm State Machine Entity instance context.
 */
typedef struct {
    uint16_t                id;                  /**< Unique Alarm Identifier */
    alarm_source_category_t source_category;     /**< Source category */
    alarm_severity_t        severity;            /**< Configured severity */
    alarm_priority_t        priority;            /**< Configured priority */
    alarm_state_t           current_state;       /**< Current FSM state */

    bool                    is_enabled;          /**< If false, updates are suppressed */
    bool                    is_latch_enabled;    /**< If true, deassertion transitions to LATCHED */
    bool                    is_ack_supported;    /**< If true, acknowledge() is permitted */

    uint64_t                triggered_timestamp_ms; /**< Uptime of last activation */
    uint64_t                cleared_timestamp_ms;   /**< Uptime of last clearance */
    float                   last_trigger_value;     /**< Engineering value triggering event */
    uint32_t                trigger_count;          /**< Cumulative activation transitions */
} alarm_entity_t;

/**
 * @brief Initialize an alarm entity with configured attributes.
 */
void alarm_entity_init(alarm_entity_t *self,
                       uint16_t id,
                       alarm_source_category_t source,
                       alarm_severity_t severity,
                       alarm_priority_t priority,
                       bool is_latch_enabled,
                       bool is_ack_supported);

/**
 * @brief Enable or disable evaluation of the entity.
 */
void alarm_entity_set_enabled(alarm_entity_t *self, bool enabled);

/**
 * @brief Evaluate condition state and execute one FSM step.
 *
 * @param[in,out] self             Pointer to entity instance.
 * @param[in]     is_asserted      True if monitored condition is currently active.
 * @param[in]     trigger_val      Measured value causing trigger (or 0.0f).
 * @param[in]     timestamp_ms     Current system uptime in milliseconds.
 * @param[out]    out_event        Populated with transition event if state changed.
 * @return true if a state transition occurred; false if state remained unchanged.
 */
bool alarm_entity_update_condition(alarm_entity_t *self,
                                   bool is_asserted,
                                   float trigger_val,
                                   uint64_t timestamp_ms,
                                   alarm_transition_event_t *out_event);

/**
 * @brief Acknowledge an active alarm.
 *
 * Transitions ACTIVE -> ACKNOWLEDGED. Has no effect in other states.
 */
bool alarm_entity_acknowledge(alarm_entity_t *self,
                              uint64_t timestamp_ms,
                              alarm_transition_event_t *out_event);

/**
 * @brief Reset a latched alarm back to INACTIVE.
 *
 * Transitions LATCHED -> INACTIVE.
 */
bool alarm_entity_reset_latch(alarm_entity_t *self,
                              uint64_t timestamp_ms,
                              alarm_transition_event_t *out_event);

/**
 * @brief Check if entity is currently in an active or latched state.
 */
static inline bool alarm_entity_is_active(const alarm_entity_t *self)
{
    if (self == NULL) {
        return false;
    }
    return (self->current_state == ALARM_STATE_ACTIVE ||
            self->current_state == ALARM_STATE_ACKNOWLEDGED ||
            self->current_state == ALARM_STATE_LATCHED);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_ALARM_ENTITY_H_ */
