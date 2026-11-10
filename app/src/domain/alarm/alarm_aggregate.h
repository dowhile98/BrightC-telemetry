/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_aggregate.h
 * @brief Pure Domain Entity: Multi-Source Aggregated Alarm Tracker.
 *
 * ISO C99 only. Zero Zephyr RTOS or vendor hardware dependencies.
 *
 * Aggregation Logic:
 * - OR Activation: Transitions INACTIVE -> ACTIVE on the FIRST source that trips.
 * - Source Contribution: Tracks active sources in a bitmask. No duplicate ACTIVATED events.
 * - AND Clearance: Transitions ACTIVE -> INACTIVE if and ONLY if ALL sources recover.
 * - Zero Flapping: Recovery of one source while another is active produces zero deactivation events.
 */

#ifndef APP_DOMAIN_ALARM_AGGREGATE_H_
#define APP_DOMAIN_ALARM_AGGREGATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/alarm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Aggregated Alarm Entity instance context.
 */
typedef struct {
    uint16_t                id;                        /**< Aggregate Alarm Identifier */
    alarm_source_category_t source_category;           /**< Origin category */
    alarm_severity_t        severity;                  /**< Severity */
    alarm_priority_t        priority;                  /**< Priority */
    alarm_state_t           current_state;             /**< FSM state */

    uint32_t                assigned_sources_mask;     /**< Bitmask of sources participating in group */
    uint32_t                contributing_sources_mask; /**< Bitmask of sources currently in alarm */

    bool                    is_enabled;                /**< Enabled flag */
    bool                    is_latch_enabled;          /**< Latching flag */

    uint64_t                triggered_timestamp_ms;    /**< Uptime of first activation */
    uint64_t                cleared_timestamp_ms;      /**< Uptime when all sources cleared */
    float                   last_trigger_value;        /**< Value of triggering source */
    uint32_t                trigger_count;             /**< Cumulative activations */
} alarm_aggregate_entity_t;

/**
 * @brief Initialize an aggregated alarm entity.
 */
void alarm_aggregate_init(alarm_aggregate_entity_t *self,
                          uint16_t id,
                          alarm_source_category_t source,
                          alarm_severity_t severity,
                          alarm_priority_t priority,
                          uint32_t assigned_sources_mask,
                          bool is_latch_enabled);

/**
 * @brief Update the condition of a single assigned source within the aggregate.
 *
 * @param[in,out] self             Pointer to entity instance.
 * @param[in]     source_index     Index of the source (0..31).
 * @param[in]     is_asserted      True if source is currently in alarm condition.
 * @param[in]     trigger_val      Measured value causing trigger.
 * @param[in]     timestamp_ms     Current system uptime in milliseconds.
 * @param[out]    out_event        Populated with transition event if state changed.
 * @return true if a state transition occurred; false otherwise.
 */
bool alarm_aggregate_update_source(alarm_aggregate_entity_t *self,
                                   uint8_t source_index,
                                   bool is_asserted,
                                   float trigger_val,
                                   uint64_t timestamp_ms,
                                   alarm_transition_event_t *out_event);

/**
 * @brief Reset a latched aggregated alarm.
 */
bool alarm_aggregate_reset_latch(alarm_aggregate_entity_t *self,
                                 uint64_t timestamp_ms,
                                 alarm_transition_event_t *out_event);

/**
 * @brief Return number of sources currently contributing to the aggregate alarm.
 */
size_t alarm_aggregate_get_active_source_count(const alarm_aggregate_entity_t *self);

/**
 * @brief Check if aggregate entity is currently active.
 */
static inline bool alarm_aggregate_is_active(const alarm_aggregate_entity_t *self)
{
    if (self == NULL) {
        return false;
    }
    return (self->current_state == ALARM_STATE_ACTIVE ||
            self->current_state == ALARM_STATE_LATCHED);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_ALARM_AGGREGATE_H_ */
