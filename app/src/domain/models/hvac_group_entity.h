/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file hvac_group_entity.h
 * @brief Pure domain model entity representing an HVAC coordination group.
 *
 * Coordinates up to HVAC_MAX_UNITS_TOTAL (8) AC units per group, implementing
 * generic N+M redundancy rotation, anti-inrush staggered delays, two-stage cooling
 * timeout rescue, multi-cause bypass evaluation, and fire emergency shutdown.
 *
 * Pure C99/C11: zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_DOMAIN_MODELS_HVAC_GROUP_ENTITY_H_
#define APP_DOMAIN_MODELS_HVAC_GROUP_ENTITY_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/hvac_types.h"
#include "domain/models/hvac_unit_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Temperature aggregation metrics input for a group.
 */
typedef struct {
    float   highest_temp;  /**< Highest observed temperature among healthy sensors (°C) */
    float   lowest_temp;   /**< Lowest observed temperature among healthy sensors (°C) */
    float   average_temp;  /**< Average observed temperature among healthy sensors (°C) */
    uint8_t valid_sensors; /**< Count of active, healthy sensors */
    uint8_t total_sensors; /**< Total sensors assigned to this group */
} group_temperature_metrics_t;

/**
 * @brief HVAC group aggregate root entity structure.
 */
typedef struct {
    /* Identification & State */
    uint8_t             group_id;           /**< Zero-indexed group identifier */
    hvac_group_state_t  state;              /**< Current group FSM state */
    uint32_t            bypass_causes_mask; /**< Active bypass causes bitmask (@see hvac_bypass_cause_t) */

    /* Managed Unit Entities */
    hvac_unit_entity_t *units[HVAC_MAX_UNITS_TOTAL]; /**< Pointers to assigned unit entities */
    uint8_t             unit_count;                  /**< Number of assigned units (0 .. 8) */

    /* Temperature Setpoints (Celsius) */
    float temp_min;    /**< Target low setpoint: cooling stops when T <= temp_min */
    float temp_max;    /**< Target high setpoint: cooling starts when T >= temp_max */
    float temp_alarm;  /**< High temp boost setpoint: all units start when T >= temp_alarm */
    float temp_bypass; /**< Critical bypass setpoint: bypass trips when T >= temp_bypass */

    /* Timers & Timeouts (seconds) */
    uint16_t delay_on_s;            /**< Anti-inrush delay between starting sequential units */
    uint16_t cooling_timeout_s;    /**< Maximum duration permitted to bring room to temp_min */
    uint16_t cooling_timer_s;      /**< Accumulated elapsed cooling time in current cycle */
    uint8_t  cooling_timeout_count; /**< Number of consecutive cooling timeouts encountered */

    /* Generic N+M Rotation Parameters */
    uint8_t  active_turn_count;      /**< Number of units actively running in turn (N) */
    uint8_t  current_turn_offset;    /**< Circular head pointer for duty shift rotation */
    uint16_t rotation_interval_days; /**< Configured days between duty shift rotations */
    uint32_t rotation_timer_s;       /**< Accumulated elapsed seconds in current rotation interval */

    /* Sensor Range & Metric Policy */
    uint8_t metric_policy; /**< Temperature selection policy (MAX, AVG, MIN) */
    uint8_t sensor_start;  /**< Range start sensor ID */
    uint8_t sensor_end;    /**< Range end sensor ID */

    /* Feature Flags & Manual Override */
    bool     enable_fan_delay;  /**< Fan purge sub-FSM enabled */
    bool     is_fire_active;    /**< Emergency fire interlock active */
    bool     is_manual_active;  /**< Manual override mode active */
    uint16_t manual_timer_s;    /**< Manual override remaining countdown (seconds) */
    uint16_t manual_timeout_s;  /**< Automatic reversion timeout for manual mode (seconds) */
} hvac_group_entity_t;

/**
 * @brief Initialize an HVAC group entity.
 *
 * @param[out] self      Group entity instance.
 * @param[in]  group_id  Zero-indexed group identifier.
 */
void hvac_group_entity_init(hvac_group_entity_t *self, uint8_t group_id);

/**
 * @brief Configure the group using persistent configuration parameters.
 *
 * @param[in,out] self Group entity instance.
 * @param[in]     cfg  Pointer to configuration structure.
 */
void hvac_group_entity_set_config(hvac_group_entity_t *self, const config_hvac_group_t *cfg);

/**
 * @brief Register an AC unit entity into this coordination group.
 *
 * @param[in,out] self Group entity instance.
 * @param[in]     unit Pointer to unit entity instance.
 * @return true if added successfully, false if group is full or parameters NULL.
 */
bool hvac_group_entity_add_unit(hvac_group_entity_t *self, hvac_unit_entity_t *unit);

/**
 * @brief Set or clear a specific bypass cause flag in the bitmask.
 *
 * @param[in,out] self    Group entity instance.
 * @param[in]     cause   Bypass cause flag to modify.
 * @param[in]     active  true to set cause, false to clear.
 */
void hvac_group_entity_set_bypass_cause(hvac_group_entity_t *self, hvac_bypass_cause_t cause, bool active);

/**
 * @brief Set or clear the emergency fire shutdown interlock.
 *
 * @param[in,out] self   Group entity instance.
 * @param[in]     active true when fire alarm is active, false when normal.
 */
void hvac_group_entity_set_fire(hvac_group_entity_t *self, bool active);

/**
 * @brief Enable or disable manual override mode.
 *
 * @param[in,out] self   Group entity instance.
 * @param[in]     active true to force manual mode ON, false to cancel manual mode.
 */
void hvac_group_entity_set_manual(hvac_group_entity_t *self, bool active);

/**
 * @brief Evaluate thermal metrics and update group and unit state transitions.
 *
 * @param[in,out] self     Group entity instance.
 * @param[in]     metrics  Aggregated temperature metrics (highest_temp, valid_sensors).
 */
void hvac_group_entity_evaluate(hvac_group_entity_t *self, const group_temperature_metrics_t *metrics);

/**
 * @brief 1-second periodic tick handler for timers, cooling timeouts, and unit ticking.
 *
 * @param[in,out] self Group entity instance.
 */
void hvac_group_entity_tick_1s(hvac_group_entity_t *self);

/**
 * @brief Circularly rotate the active turn offset (N+M duty shift).
 *
 * Shifts current_turn_offset to the next unit and re-dispatches active and standby units.
 *
 * @param[in,out] self Group entity instance.
 */
void hvac_group_entity_rotate_shift(hvac_group_entity_t *self);

/**
 * @brief Get the accumulated elapsed seconds of the current rotation interval.
 *
 * @param[in] self Group entity instance.
 * @return Elapsed seconds since last rotation.
 */
uint32_t hvac_group_entity_get_rotation_timer(const hvac_group_entity_t *self);

/**
 * @brief Get configured rotation interval in days.
 *
 * @param[in] self Group entity instance.
 * @return Rotation interval in days.
 */
uint16_t hvac_group_entity_get_rotation_interval_days(const hvac_group_entity_t *self);

/**
 * @brief Set rotation interval in days and reset rotation timer.
 *
 * @param[in,out] self Group entity instance.
 * @param[in]     days Interval in days.
 */
void hvac_group_entity_set_rotation_interval_days(hvac_group_entity_t *self, uint16_t days);

/**
 * @brief Compute aggregated group temperature metrics from a raw/calibrated sensor array.
 *
 * Aggregates readings for all channels enabled in @p sensor_mask.
 * Hardware-agnostic: operates strictly on floating-point temperatures in Celsius.
 *
 * @param[in]  temperatures Temperature array in °C.
 * @param[in]  valid_mask   Bitmask indicating healthy/valid sensors (bit s = 1).
 * @param[in]  temp_count   Number of elements in @p temperatures.
 * @param[in]  sensor_mask  Bitmask of sensors assigned to this group.
 * @param[out] out_metrics  Destination for computed metrics.
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int hvac_compute_group_metrics(const float *temperatures,
                               uint16_t valid_mask,
                               size_t temp_count,
                               uint16_t sensor_mask,
                               group_temperature_metrics_t *out_metrics);

/**
 * @brief Compute aggregated group temperature metrics from a flat sensor ID range.
 *
 * Aggregates readings for all channels in the range [sensor_start, sensor_end] (1-based, 1..59).
 * Hardware-agnostic: operates strictly on floating-point temperatures in Celsius.
 *
 * @param[in]  temperatures Temperature array in °C.
 * @param[in]  valid_mask   64-bit mask indicating healthy/valid sensors (bit id-1 = 1).
 * @param[in]  temp_count   Number of elements in @p temperatures.
 * @param[in]  sensor_start Range start flat sensor ID (1..59).
 * @param[in]  sensor_end   Range end flat sensor ID (1..59).
 * @param[out] out_metrics  Destination for computed metrics.
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int hvac_compute_group_metrics_range(const float *temperatures,
                                     uint64_t valid_mask,
                                     size_t temp_count,
                                     uint8_t sensor_start,
                                     uint8_t sensor_end,
                                     group_temperature_metrics_t *out_metrics);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_MODELS_HVAC_GROUP_ENTITY_H_ */
