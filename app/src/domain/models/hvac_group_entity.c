/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file hvac_group_entity.c
 * @brief Pure domain model implementation for an HVAC coordination group.
 */

#include "domain/models/hvac_group_entity.h"
#include <string.h>

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
    self->enable_fan_delay = true;
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
    self->enable_fan_delay = (cfg->enable_fan_delay != 0U);

    for (uint8_t i = 0U; i < self->unit_count; i++) {
        if (self->units[i] != NULL) {
            self->units[i]->startup_delay_s = cfg->delay_on_s;
            self->units[i]->fan_delay_s = cfg->fan_delay_s;
            self->units[i]->pressure_retry_s = cfg->pressure_retry_s;
            self->units[i]->max_allowed_faults = cfg->max_allowed_faults;
            self->units[i]->enable_fan_delay = (cfg->enable_fan_delay != 0U);
        }
    }
}

bool hvac_group_entity_add_unit(hvac_group_entity_t *self, hvac_unit_entity_t *unit)
{
    if (self == NULL || unit == NULL || self->unit_count >= HVAC_MAX_UNITS_TOTAL) {
        return false;
    }

    self->units[self->unit_count++] = unit;
    return true;
}

void hvac_group_entity_set_bypass_cause(hvac_group_entity_t *self, hvac_bypass_cause_t cause, bool active)
{
    if (self == NULL) {
        return;
    }

    if (active) {
        self->bypass_causes_mask |= (uint32_t)cause;
        if (!self->is_fire_active) {
            self->state = GROUP_STATE_BYPASS;
            self->cooling_timer_s = 0U;
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

    self->is_fire_active = active;
    if (active) {
        self->state = GROUP_STATE_FIRE;
        self->cooling_timer_s = 0U;
        self->cooling_timeout_count = 0U;
        for (uint8_t i = 0U; i < self->unit_count; i++) {
            if (self->units[i] != NULL) {
                hvac_unit_entity_request_stop(self->units[i], false);
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

    self->is_manual_active = active;
    if (active) {
        self->manual_timer_s = self->manual_timeout_s;
        if (!self->is_fire_active && (self->bypass_causes_mask == 0U)) {
            for (uint8_t i = 0U; i < self->unit_count; i++) {
                if (self->units[i] != NULL) {
                    hvac_unit_entity_set_manual(self->units[i], true);
                }
            }
        }
    } else {
        self->manual_timer_s = 0U;
        if (!self->is_fire_active && (self->bypass_causes_mask == 0U)) {
            for (uint8_t i = 0U; i < self->unit_count; i++) {
                if (self->units[i] != NULL) {
                    hvac_unit_entity_set_manual(self->units[i], false);
                }
            }
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
        return;
    }

    /* 2. Sensor health evaluation */
    if (metrics == NULL || metrics->valid_sensors == 0U) {
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

    /* 4. High-temperature bypass trigger and hysteresis */
    if (metrics->highest_temp >= self->temp_bypass) {
        hvac_group_entity_set_bypass_cause(self, BYPASS_CAUSE_HIGH_TEMP, true);
    } else if ((self->bypass_causes_mask & (uint32_t)BYPASS_CAUSE_HIGH_TEMP) != 0U) {
        if (metrics->highest_temp <= self->temp_min) {
            hvac_group_entity_set_bypass_cause(self, BYPASS_CAUSE_HIGH_TEMP, false);
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
    if (self->is_manual_active) {
        return;
    }

    /* 7. Thermal FSM sequencing */
    if (metrics->highest_temp <= self->temp_min) {
        self->state = GROUP_STATE_IDLE;
        self->cooling_timer_s = 0U;
        self->cooling_timeout_count = 0U;
        for (uint8_t i = 0U; i < self->unit_count; i++) {
            if (self->units[i] != NULL) {
                hvac_unit_entity_request_stop(self->units[i], self->enable_fan_delay);
            }
        }
    } else if ((metrics->highest_temp >= self->temp_alarm) || (self->cooling_timeout_count == 1U)) {
        self->state = GROUP_STATE_COOL_BOOST;
        for (uint8_t k = 0U; k < self->unit_count; k++) {
            uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
            if (self->units[idx] != NULL) {
                hvac_unit_entity_request_start_staggered(self->units[idx], (uint16_t)(k * self->delay_on_s));
            }
        }
    } else if ((metrics->highest_temp >= self->temp_max) || (self->state == GROUP_STATE_COOLING)) {
        self->state = GROUP_STATE_COOLING;
        for (uint8_t k = 0U; k < self->unit_count; k++) {
            uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
            if (self->units[idx] != NULL) {
                if (k < self->active_turn_count) {
                    hvac_unit_entity_request_start_staggered(self->units[idx], (uint16_t)(k * self->delay_on_s));
                } else {
                    hvac_unit_entity_request_stop(self->units[idx], self->enable_fan_delay);
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

    /* 2. Manual override timer */
    if (self->is_manual_active) {
        if (self->manual_timer_s > 0U) {
            self->manual_timer_s--;
        }
        if (self->manual_timer_s == 0U) {
            self->is_manual_active = false;
        }
    }

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
                     */
                    for (uint8_t k = 0U; (k < self->active_turn_count) && (k < self->unit_count); k++) {
                        uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
                        if (self->units[idx] != NULL) {
                            hvac_unit_entity_increment_fault(self->units[idx]);
                        }
                    }

                    /* Rescue boost: force all N+M units ON with staggered delays */
                    self->state = GROUP_STATE_COOL_BOOST;
                    for (uint8_t k = 0U; k < self->unit_count; k++) {
                        uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
                        if (self->units[idx] != NULL) {
                            hvac_unit_entity_request_start_staggered(self->units[idx], (uint16_t)(k * self->delay_on_s));
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

void hvac_group_entity_rotate_shift(hvac_group_entity_t *self)
{
    if (self == NULL || self->unit_count == 0U) {
        return;
    }

    self->current_turn_offset = (uint8_t)((self->current_turn_offset + 1U) % self->unit_count);

    if (self->state == GROUP_STATE_COOLING) {
        for (uint8_t k = 0U; k < self->unit_count; k++) {
            uint8_t idx = (self->current_turn_offset + k) % self->unit_count;
            if (self->units[idx] != NULL) {
                if (k < self->active_turn_count) {
                    hvac_unit_entity_request_start_staggered(self->units[idx], (uint16_t)(k * self->delay_on_s));
                } else {
                    hvac_unit_entity_request_stop(self->units[idx], self->enable_fan_delay);
                }
            }
        }
    }
}
