/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file hvac_group_entity.c
 * @brief Pure domain model implementation for an HVAC coordination group.
 */

#include "domain/models/hvac_group_entity.h"
#include <string.h>

static uint8_t count_starting_units(const hvac_group_entity_t *self)
{
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < self->unit_count; i++) {
        if (self->units[i] != NULL && self->units[i]->state == UNIT_STATE_STARTING) {
            count++;
        }
    }
    return count;
}

static bool is_unit_available(const hvac_unit_entity_t *unit)
{
    if (unit == NULL) {
        return false;
    }
    if (unit->state == UNIT_STATE_FAULT ||
        unit->state == UNIT_STATE_BLOCKED_ALARM ||
        unit->state == UNIT_STATE_BYPASS) {
        return false;
    }
    if (unit->high_pressure_alarm ||
        unit->low_pressure_alarm ||
        unit->supply_fault_alarm ||
        unit->voltage_fault_alarm) {
        return false;
    }
    return true;
}

static void reconcile_unit_fan_state(hvac_unit_entity_t *unit, bool enable_fan_delay)
{
    if (unit == NULL) {
        return;
    }
    unit->enable_fan_delay = enable_fan_delay;
    if (unit->fan_state == HVAC_FAN_STATE_FIRE) {
        return;
    }
    if (!enable_fan_delay) {
        if (unit->state == UNIT_STATE_STOPPING) {
            unit->state = UNIT_STATE_OFF;
        }
        unit->fan_delay_timer_s = 0U;
        if (unit->state != UNIT_STATE_RUNNING) {
            unit->fan_state = HVAC_FAN_STATE_DISABLED;
        }
    } else {
        if (unit->state == UNIT_STATE_OFF ||
            unit->state == UNIT_STATE_STARTING ||
            unit->state == UNIT_STATE_BLOCKED_ALARM ||
            unit->state == UNIT_STATE_FAULT) {
            unit->fan_state = HVAC_FAN_STATE_SMART_IDLE_ON;
        }
    }
}

void hvac_group_entity_init(hvac_group_entity_t *self, uint8_t group_id)
{
    if (self == NULL) {
        return;
    }

    memset(self, 0, sizeof(*self));
    self->group_id = group_id;
    self->state = GROUP_STATE_IDLE;
    self->bypass_causes_mask = (uint32_t)BYPASS_CAUSE_NONE;

    /* Safe industry defaults */
    self->temp_min = 21.0f;
    self->temp_max = 24.0f;
    self->temp_alarm = 27.0f;
    self->temp_bypass = 32.0f;

    self->cooling_timeout_s = 3600U;
    self->delay_on_s = 20U;
    self->manual_timeout_s = 1800U;

    self->active_turn_count = 1U;
    self->current_turn_offset = 0U;
    self->rotation_interval_days = 7U;
    self->rotation_timer_s = 0U;
    self->enable_fan_delay = false;
    self->is_mode_off = false;

    self->metric_policy = (uint8_t)SENSOR_METRIC_MAX;
    self->sensor_start = 0U;
    self->sensor_end = 0U;
}

void hvac_group_entity_set_config(hvac_group_entity_t *self, const config_hvac_group_t *cfg)
{
    if (self == NULL || cfg == NULL) {
        return;
    }

    self->temp_min = cfg->temp_min;
    self->temp_max = cfg->temp_max;
    self->temp_alarm = cfg->temp_alarm;
    self->temp_bypass = cfg->temp_bypass;

    self->cooling_timeout_s = cfg->time_to_cool_s;
    self->delay_on_s = cfg->delay_on_s;
    self->manual_timeout_s = cfg->manual_timeout_s;
    self->active_turn_count = cfg->active_turn_count;
    self->rotation_interval_days = cfg->rotation_interval_days;
    self->rotation_timer_s = 0U;
    self->enable_fan_delay = (cfg->enable_fan_delay != 0U);

    self->metric_policy = cfg->metric_policy;
    self->sensor_start = cfg->sensor_start;
    self->sensor_end = cfg->sensor_end;

    for (uint8_t i = 0U; i < self->unit_count; i++) {
        if (self->units[i] != NULL) {
            self->units[i]->startup_delay_s = cfg->delay_on_s;
            self->units[i]->fan_delay_s = cfg->fan_delay_s;
            self->units[i]->pressure_retry_s = cfg->pressure_retry_s;
            self->units[i]->max_allowed_faults = cfg->max_allowed_faults;
            reconcile_unit_fan_state(self->units[i], self->enable_fan_delay);
        }
    }
}

bool hvac_group_entity_add_unit(hvac_group_entity_t *self, hvac_unit_entity_t *unit)
{
    if (self == NULL || unit == NULL || self->unit_count >= HVAC_MAX_UNITS_TOTAL) {
        return false;
    }

    reconcile_unit_fan_state(unit, self->enable_fan_delay);
    self->units[self->unit_count++] = unit;
    return true;
}

void hvac_group_entity_clear_units(hvac_group_entity_t *self)
{
    if (self == NULL) {
        return;
    }

    self->unit_count = 0U;
    memset(self->units, 0, sizeof(self->units));
}

void hvac_group_entity_set_bypass_cause(hvac_group_entity_t *self, hvac_bypass_cause_t cause, bool active)
{
    if (self == NULL) {
        return;
    }

    if (active) {
        self->bypass_causes_mask |= (uint32_t)cause;
        self->cooling_timer_s = 0U;
        if (cause != BYPASS_CAUSE_COOLING_TIMEOUT) {
            self->cooling_timeout_count = 0U;
        }
        if (self->is_manual_active) {
            self->is_manual_active = false;
            self->manual_timer_s = 0U;
        }
        if (!self->is_fire_active) {
            self->state = GROUP_STATE_BYPASS;
            for (uint8_t i = 0U; i < self->unit_count; i++) {
                if (self->units[i] != NULL) {
                    hvac_unit_entity_set_bypass(self->units[i]);
                }
            }
        }
    } else {
        self->bypass_causes_mask &= ~(uint32_t)cause;
        if (self->bypass_causes_mask == 0U && self->state == GROUP_STATE_BYPASS && !self->is_fire_active) {
            self->state = GROUP_STATE_IDLE;
            self->cooling_timer_s = 0U;
            self->cooling_timeout_count = 0U;
            for (uint8_t i = 0U; i < self->unit_count; i++) {
                if (self->units[i] != NULL) {
                    hvac_unit_entity_request_stop(self->units[i], self->enable_fan_delay);
                }
            }
        }
    }
}

void hvac_group_entity_set_fire(hvac_group_entity_t *self, bool active)
{
    if (self == NULL) {
        return;
    }

    if (self->is_fire_active == active) {
        return;
    }

    self->is_fire_active = active;
    self->cooling_timer_s = 0U;
    self->cooling_timeout_count = 0U;
    if (active) {
        self->state = GROUP_STATE_FIRE;
        for (uint8_t i = 0U; i < self->unit_count; i++) {
            if (self->units[i] != NULL) {
                hvac_unit_entity_set_manual(self->units[i], false);
                self->units[i]->state = UNIT_STATE_OFF;
                self->units[i]->fan_state = HVAC_FAN_STATE_FIRE;
                self->units[i]->startup_timer_s = 0U;
                self->units[i]->fan_delay_timer_s = 0U;
            }
        }
    } else {
        if (self->bypass_causes_mask != 0U) {
            self->state = GROUP_STATE_BYPASS;
            for (uint8_t i = 0U; i < self->unit_count; i++) {
                if (self->units[i] != NULL) {
                    hvac_unit_entity_set_bypass(self->units[i]);
                }
            }
        } else {
            self->state = GROUP_STATE_IDLE;
            for (uint8_t i = 0U; i < self->unit_count; i++) {
                if (self->units[i] != NULL) {
                    self->units[i]->fan_state = self->enable_fan_delay
                                                    ? HVAC_FAN_STATE_SMART_IDLE_ON
                                                    : HVAC_FAN_STATE_DISABLED;
                    hvac_unit_entity_request_stop(self->units[i], self->enable_fan_delay);
                }
            }
        }
    }
}

void hvac_group_entity_set_manual(hvac_group_entity_t *self, bool active)
{
    if (self == NULL) {
        return;
    }

    if (!active && !self->is_manual_active && self->state != GROUP_STATE_MANUAL) {
        return;
    }

    self->is_manual_active = active;
    if (active) {
        self->state = GROUP_STATE_MANUAL;
        self->manual_timer_s = self->manual_timeout_s;
        self->cooling_timer_s = 0U;
        self->cooling_timeout_count = 0U;
        /* DO NOT turn on all units! Units should remain in their current state or only be controlled individually. */
    } else {
        self->manual_timer_s = 0U;
        if (self->state == GROUP_STATE_MANUAL) {
            self->state = GROUP_STATE_IDLE;
            self->cooling_timer_s = 0U;
            self->cooling_timeout_count = 0U;
        }
        for (uint8_t i = 0U; i < self->unit_count; i++) {
            if (self->units[i] != NULL) {
                hvac_unit_entity_set_manual(self->units[i], false);
                hvac_unit_entity_request_stop(self->units[i], self->enable_fan_delay);
            }
        }
    }
}

void hvac_group_entity_set_mode_off(hvac_group_entity_t *self, bool off)
{
    if (self == NULL) {
        return;
    }

    if (off) {
        self->is_mode_off = true;
        self->state = GROUP_STATE_OFF;
        for (uint8_t i = 0U; i < self->unit_count; i++) {
            if (self->units[i] != NULL) {
                hvac_unit_entity_set_manual(self->units[i], false);
                hvac_unit_entity_request_stop(self->units[i], self->enable_fan_delay);
            }
        }
        self->cooling_timer_s = 0U;
        self->cooling_timeout_count = 0U;
        self->manual_timer_s = 0U;
    } else {
        self->is_mode_off = false;
        if (self->state == GROUP_STATE_OFF) {
            self->state = GROUP_STATE_IDLE;
        }
    }
}

void hvac_group_entity_evaluate(hvac_group_entity_t *self, const group_temperature_metrics_t *metrics)
{
    if (self == NULL) {
        return;
    }

    /* 1. Fire emergency interlock: absolute highest priority */
    if (self->is_fire_active) {
        self->state = GROUP_STATE_FIRE;
        for (uint8_t i = 0U; i < self->unit_count; i++) {
            if (self->units[i] != NULL) {
                self->units[i]->state = UNIT_STATE_OFF;
                self->units[i]->fan_state = HVAC_FAN_STATE_FIRE;
            }
        }
        return;
    }

    /* Permanent Mode OFF: keep group OFF and all units stopped */
    if (self->is_mode_off || self->state == GROUP_STATE_OFF) {
        self->state = GROUP_STATE_OFF;
        for (uint8_t i = 0U; i < self->unit_count; i++) {
            if (self->units[i] != NULL) {
                hvac_unit_entity_request_stop(self->units[i], self->enable_fan_delay);
            }
        }
        return;
    }

    /* 2. Sensor health evaluation */
    if (metrics == NULL || metrics->valid_sensors == 0U) {
        self->bypass_causes_mask &= ~(uint32_t)BYPASS_CAUSE_HIGH_TEMP;
        hvac_group_entity_set_bypass_cause(self, BYPASS_CAUSE_SENSOR_FAULT, true);
        return;
    } else {
        hvac_group_entity_set_bypass_cause(self, BYPASS_CAUSE_SENSOR_FAULT, false);
    }

    /* 3. Evaluate unit health (all faults check) */
    if (self->unit_count > 0U) {
        bool all_faulted = true;
        for (uint8_t i = 0U; i < self->unit_count; i++) {
            if (self->units[i] != NULL && self->units[i]->state != UNIT_STATE_FAULT) {
                all_faulted = false;
                break;
            }
        }
        if (all_faulted) {
            hvac_group_entity_set_bypass_cause(self, BYPASS_CAUSE_ALL_FAULTS, true);
            return;
        } else {
            hvac_group_entity_set_bypass_cause(self, BYPASS_CAUSE_ALL_FAULTS, false);
        }
    }

    /* Compute controlling temperature based on configured metric policy */
    float ctrl_temp = metrics->highest_temp;
    if (self->metric_policy == (uint8_t)SENSOR_METRIC_AVG) {
        ctrl_temp = metrics->average_temp;
    } else if (self->metric_policy == (uint8_t)SENSOR_METRIC_MIN) {
        ctrl_temp = metrics->lowest_temp;
    }

    /* 4. High-temperature bypass trigger and recovery when all temperatures drop <= temp_min */
    if (ctrl_temp >= self->temp_bypass) {
        hvac_group_entity_set_bypass_cause(self, BYPASS_CAUSE_HIGH_TEMP, true);
    }

    if ((ctrl_temp <= self->temp_min) && (metrics->highest_temp <= self->temp_min)) {
        const uint32_t thermal_bypass_mask =
            (uint32_t)BYPASS_CAUSE_HIGH_TEMP | (uint32_t)BYPASS_CAUSE_COOLING_TIMEOUT;
        if ((self->bypass_causes_mask & thermal_bypass_mask) != 0U) {
            if ((self->bypass_causes_mask & (uint32_t)BYPASS_CAUSE_HIGH_TEMP) != 0U) {
                hvac_group_entity_set_bypass_cause(self, BYPASS_CAUSE_HIGH_TEMP, false);
            }
            if ((self->bypass_causes_mask & (uint32_t)BYPASS_CAUSE_COOLING_TIMEOUT) != 0U) {
                hvac_group_entity_set_bypass_cause(self, BYPASS_CAUSE_COOLING_TIMEOUT, false);
            }
            self->cooling_timeout_count = 0U;
            self->cooling_timer_s = 0U;
            if (self->unit_count > 0U) {
                self->current_turn_offset =
                    (uint8_t)((self->current_turn_offset + 1U) % self->unit_count);
            }
        }
    }

    /* 5. Check if any bypass causes remain active */
    if (self->bypass_causes_mask != 0U) {
        self->state = GROUP_STATE_BYPASS;
        self->cooling_timer_s = 0U;
        for (uint8_t i = 0U; i < self->unit_count; i++) {
            if (self->units[i] != NULL) {
                hvac_unit_entity_set_bypass(self->units[i]);
            }
        }
        return;
    }

    /* 6. Manual mode evaluation */
    if (self->is_manual_active || self->state == GROUP_STATE_MANUAL) {
        self->state = GROUP_STATE_MANUAL;
        self->cooling_timer_s = 0U;
        self->cooling_timeout_count = 0U;
        return;
    }

    /* 7. Thermal FSM sequencing */
    if (ctrl_temp <= self->temp_min) {
        self->state = GROUP_STATE_IDLE;
        self->cooling_timer_s = 0U;
        self->cooling_timeout_count = 0U;
        for (uint8_t i = 0U; i < self->unit_count; i++) {
            if (self->units[i] != NULL) {
                hvac_unit_entity_request_stop(self->units[i], self->enable_fan_delay);
            }
        }
    } else if ((ctrl_temp >= self->temp_alarm) ||
               (self->cooling_timeout_count == 1U) ||
               (self->state == GROUP_STATE_COOL_BOOST)) {
        if (self->state != GROUP_STATE_COOL_BOOST) {
            self->cooling_timer_s = 0U;
        }
        self->state = GROUP_STATE_COOL_BOOST;
        uint8_t starting_delay_step = count_starting_units(self);
        for (uint8_t k = 0U; k < self->unit_count; k++) {
            uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
            hvac_unit_entity_t *unit = self->units[idx];
            if (unit != NULL) {
                bool is_available = is_unit_available(unit);
                if (is_available) {
                    if (unit->state == UNIT_STATE_RUNNING || unit->state == UNIT_STATE_STARTING) {
                        (void)hvac_unit_entity_request_start_staggered(unit, 0U);
                    } else {
                        uint16_t delay = (uint16_t)(starting_delay_step * self->delay_on_s);
                        if (hvac_unit_entity_request_start_staggered(unit, delay)) {
                            starting_delay_step++;
                        }
                    }
                }
            }
        }
    } else if ((ctrl_temp >= self->temp_max) || (self->state == GROUP_STATE_COOLING)) {
        self->state = GROUP_STATE_COOLING;
        uint8_t active_assigned = 0U;
        uint8_t starting_delay_step = count_starting_units(self);
        for (uint8_t k = 0U; k < self->unit_count; k++) {
            uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
            hvac_unit_entity_t *unit = self->units[idx];
            if (unit != NULL) {
                bool is_available = is_unit_available(unit);
                if (is_available && (active_assigned < self->active_turn_count)) {
                    if (unit->state == UNIT_STATE_RUNNING || unit->state == UNIT_STATE_STARTING) {
                        (void)hvac_unit_entity_request_start_staggered(unit, 0U);
                        active_assigned++;
                    } else {
                        uint16_t delay = (uint16_t)(starting_delay_step * self->delay_on_s);
                        if (hvac_unit_entity_request_start_staggered(unit, delay)) {
                            starting_delay_step++;
                            active_assigned++;
                        }
                    }
                } else {
                    hvac_unit_entity_request_stop(unit, self->enable_fan_delay);
                }
            }
        }
    }
}

void hvac_group_entity_tick_1s(hvac_group_entity_t *self)
{
    if (self == NULL) {
        return;
    }

    /* 1. Tick each assigned unit entity */
    for (uint8_t i = 0U; i < self->unit_count; i++) {
        if (self->units[i] != NULL) {
            hvac_unit_entity_tick_1s(self->units[i]);
        }
    }

    if (self->is_mode_off || self->state == GROUP_STATE_OFF) {
        return;
    }

    /* 2. Manual override timer */
    if (self->is_manual_active || self->state == GROUP_STATE_MANUAL) {
        if (self->manual_timer_s > 0U) {
            self->manual_timer_s--;
            if (self->manual_timer_s == 0U) {
                hvac_group_entity_set_manual(self, false);
            }
        }
    } else {
        /* 3. Cooling timeout and rescue boost */
        if (self->state == GROUP_STATE_COOLING || self->state == GROUP_STATE_COOL_BOOST) {
            if (self->cooling_timeout_s > 0U) {
                self->cooling_timer_s++;
                if (self->cooling_timer_s >= self->cooling_timeout_s) {
                    self->cooling_timeout_count++;
                    self->cooling_timer_s = 0U;

                    if (self->cooling_timeout_count == 1U) {
                        /* First timeout:
                         * Increment fault counter on currently active turn units
                         * (keeps them RUNNING unless fault_counter >= max_allowed_faults)
                         */
                        for (uint8_t k = 0U; (k < self->active_turn_count) && (k < self->unit_count); k++) {
                            uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
                            if (self->units[idx] != NULL) {
                                hvac_unit_entity_increment_fault(self->units[idx]);
                            }
                        }

                        /* Rescue boost: keep active units running and start backup units staggered */
                        self->state = GROUP_STATE_COOL_BOOST;
                        uint8_t starting_delay_step = count_starting_units(self);
                        for (uint8_t k = 0U; k < self->unit_count; k++) {
                            uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
                            hvac_unit_entity_t *unit = self->units[idx];
                            if (unit != NULL) {
                                bool is_available = is_unit_available(unit);
                                if (is_available) {
                                    if (unit->state == UNIT_STATE_RUNNING ||
                                        unit->state == UNIT_STATE_STARTING) {
                                        (void)hvac_unit_entity_request_start_staggered(unit, 0U);
                                    } else {
                                        uint16_t delay = (uint16_t)(starting_delay_step * self->delay_on_s);
                                        if (hvac_unit_entity_request_start_staggered(unit, delay)) {
                                            starting_delay_step++;
                                        }
                                    }
                                }
                            }
                        }
                    } else if (self->cooling_timeout_count >= 2U) {
                        /* Second timeout:
                         * Two successive failed cooling attempts engage mechanical bypass
                         */
                        hvac_group_entity_set_bypass_cause(self, BYPASS_CAUSE_COOLING_TIMEOUT, true);
                    }
                }
            }
        }
    }

    /* 4. Automatic duty rotation timer */
    if (self->rotation_interval_days > 0U) {
        self->rotation_timer_s++;
        if (self->rotation_timer_s >= ((uint32_t)self->rotation_interval_days * 86400U)) {
            self->rotation_timer_s = 0U;
            hvac_group_entity_rotate_shift(self);
        }
    }
}

void hvac_group_entity_rotate_shift(hvac_group_entity_t *self)
{
    if (self == NULL || self->unit_count == 0U) {
        return;
    }

    self->rotation_timer_s = 0U;
    self->current_turn_offset = (uint8_t)((self->current_turn_offset + 1U) % self->unit_count);

    if (self->state == GROUP_STATE_COOLING) {
        /* Stop units no longer in the active turn first */
        uint8_t active_assigned = 0U;
        for (uint8_t k = 0U; k < self->unit_count; k++) {
            uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
            hvac_unit_entity_t *unit = self->units[idx];
            if (unit != NULL) {
                bool is_available = is_unit_available(unit);
                if (is_available && (active_assigned < self->active_turn_count)) {
                    active_assigned++;
                } else {
                    hvac_unit_entity_request_stop(unit, self->enable_fan_delay);
                }
            }
        }

        /* Now start newly assigned units staggered after any already STARTING units */
        active_assigned = 0U;
        uint8_t starting_delay_step = count_starting_units(self);
        for (uint8_t k = 0U; k < self->unit_count; k++) {
            uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
            hvac_unit_entity_t *unit = self->units[idx];
            if (unit != NULL) {
                bool is_available = is_unit_available(unit);
                if (is_available && (active_assigned < self->active_turn_count)) {
                    if (unit->state == UNIT_STATE_RUNNING || unit->state == UNIT_STATE_STARTING) {
                        (void)hvac_unit_entity_request_start_staggered(unit, 0U);
                        active_assigned++;
                    } else {
                        uint16_t delay = (uint16_t)(starting_delay_step * self->delay_on_s);
                        if (hvac_unit_entity_request_start_staggered(unit, delay)) {
                            starting_delay_step++;
                            active_assigned++;
                        }
                    }
                }
            }
        }
    }
}

uint32_t hvac_group_entity_get_rotation_timer(const hvac_group_entity_t *self)
{
    return (self != NULL) ? self->rotation_timer_s : 0U;
}

uint16_t hvac_group_entity_get_rotation_interval_days(const hvac_group_entity_t *self)
{
    return (self != NULL) ? self->rotation_interval_days : 0U;
}

void hvac_group_entity_set_rotation_interval_days(hvac_group_entity_t *self, uint16_t days)
{
    if (self != NULL) {
        self->rotation_interval_days = days;
        self->rotation_timer_s = 0U;
    }
}

int hvac_compute_group_metrics(const float *temperatures,
                               uint16_t valid_mask,
                               size_t temp_count,
                               uint16_t sensor_mask,
                               group_temperature_metrics_t *out_metrics)
{
    if (temperatures == NULL || out_metrics == NULL) {
        return -EINVAL;
    }

    out_metrics->highest_temp = -999.0f;
    out_metrics->lowest_temp = 999.0f;
    out_metrics->valid_sensors = 0U;
    out_metrics->total_sensors = 0U;
    out_metrics->average_temp = 0.0f;

    float sum = 0.0f;
    size_t limit = temp_count < 16U ? temp_count : 16U;

    for (size_t s = 0U; s < limit; s++) {
        uint16_t bit = (uint16_t)(1U << s);
        if ((sensor_mask & bit) != 0U) {
            out_metrics->total_sensors++;
            if ((valid_mask & bit) != 0U) {
                out_metrics->valid_sensors++;
                float t = temperatures[s];
                sum += t;
                if (t > out_metrics->highest_temp) {
                    out_metrics->highest_temp = t;
                }
                if (t < out_metrics->lowest_temp) {
                    out_metrics->lowest_temp = t;
                }
            }
        }
    }

    if (out_metrics->valid_sensors > 0U) {
        out_metrics->average_temp = sum / (float)out_metrics->valid_sensors;
    } else {
        out_metrics->highest_temp = 0.0f;
        out_metrics->lowest_temp = 0.0f;
        out_metrics->average_temp = 0.0f;
    }

    return 0;
}

int hvac_compute_group_metrics_range(const float *temperatures,
                                     uint64_t valid_mask,
                                     size_t temp_count,
                                     uint8_t sensor_start,
                                     uint8_t sensor_end,
                                     group_temperature_metrics_t *out_metrics)
{
    if (temperatures == NULL || out_metrics == NULL) {
        return -EINVAL;
    }

    if (sensor_start == 0U || sensor_end < sensor_start || sensor_end > 64U) {
        return -EINVAL;
    }

    out_metrics->highest_temp = -999.0f;
    out_metrics->lowest_temp = 999.0f;
    out_metrics->valid_sensors = 0U;
    out_metrics->total_sensors = 0U;
    out_metrics->average_temp = 0.0f;

    float sum = 0.0f;

    for (uint8_t id = sensor_start; id <= sensor_end; id++) {
        out_metrics->total_sensors++;
        size_t idx = (size_t)(id - 1U);
        if (idx < temp_count && idx < 64U) {
            uint64_t bit = 1ULL << idx;
            if ((valid_mask & bit) != 0ULL) {
                out_metrics->valid_sensors++;
                float t = temperatures[idx];
                sum += t;
                if (t > out_metrics->highest_temp) {
                    out_metrics->highest_temp = t;
                }
                if (t < out_metrics->lowest_temp) {
                    out_metrics->lowest_temp = t;
                }
            }
        }
    }

    if (out_metrics->valid_sensors > 0U) {
        out_metrics->average_temp = sum / (float)out_metrics->valid_sensors;
    } else {
        out_metrics->highest_temp = 0.0f;
        out_metrics->lowest_temp = 0.0f;
        out_metrics->average_temp = 0.0f;
    }

    return 0;
}

