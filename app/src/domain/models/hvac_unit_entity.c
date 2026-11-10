/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file hvac_unit_entity.c
 * @brief Pure domain model implementation for an individual AC unit.
 */

#include "domain/models/hvac_unit_entity.h"
#include <string.h>

static bool has_active_alarm(const hvac_unit_entity_t *self)
{
    return (self->high_pressure_alarm ||
            self->low_pressure_alarm ||
            self->supply_fault_alarm ||
            self->voltage_fault_alarm);
}

void hvac_unit_entity_init(hvac_unit_entity_t *self,
                           uint8_t unit_id,
                           uint8_t max_faults,
                           uint16_t startup_delay_s,
                           uint16_t fan_delay_s,
                           uint16_t pressure_retry_s,
                           bool enable_fan_delay)
{
    if (self == NULL) {
        return;
    }

    memset(self, 0, sizeof(*self));
    self->unit_id = unit_id;
    self->state = UNIT_STATE_OFF;
    self->enable_fan_delay = enable_fan_delay;
    self->fan_state = enable_fan_delay ? HVAC_FAN_STATE_SMART_IDLE_ON : HVAC_FAN_STATE_DISABLED;
    self->max_allowed_faults = (max_faults > 0U) ? max_faults : 3U;
    self->startup_delay_s = startup_delay_s;
    self->fan_delay_s = fan_delay_s;
    self->pressure_retry_s = (pressure_retry_s > 0U) ? pressure_retry_s : 180U;
}

void hvac_unit_entity_update_alarms(hvac_unit_entity_t *self,
                                    bool high_press,
                                    bool low_press,
                                    bool supply_fault,
                                    bool volt_fault)
{
    if (self == NULL) {
        return;
    }

    self->high_pressure_alarm = high_press;
    self->low_pressure_alarm = low_press;
    self->supply_fault_alarm = supply_fault;
    self->voltage_fault_alarm = volt_fault;

    if (has_active_alarm(self)) {
        if (self->state == UNIT_STATE_RUNNING || self->state == UNIT_STATE_STARTING) {
            self->state = UNIT_STATE_BLOCKED_ALARM;
            self->fan_state = self->enable_fan_delay ? HVAC_FAN_STATE_SMART_IDLE_ON : HVAC_FAN_STATE_DISABLED;
            self->startup_timer_s = 0U;
            self->fan_delay_timer_s = 0U;
            self->retry_timer_s = self->pressure_retry_s;
            hvac_unit_entity_increment_fault(self);
        }
    }
}

void hvac_unit_entity_increment_fault(hvac_unit_entity_t *self)
{
    if (self == NULL) {
        return;
    }

    if (self->state != UNIT_STATE_RUNNING &&
        self->state != UNIT_STATE_STARTING &&
        self->state != UNIT_STATE_BLOCKED_ALARM) {
        return;
    }

    self->fault_counter++;

    if (self->fault_counter >= self->max_allowed_faults) {
        self->state = UNIT_STATE_FAULT;
        self->fan_state = self->enable_fan_delay ? HVAC_FAN_STATE_SMART_IDLE_ON : HVAC_FAN_STATE_DISABLED;
        self->startup_timer_s = 0U;
        self->fan_delay_timer_s = 0U;
        self->retry_timer_s = 0U;
    }
}

void hvac_unit_entity_tick_1s(hvac_unit_entity_t *self)
{
    if (self == NULL) {
        return;
    }

    switch (self->state) {
    case UNIT_STATE_STARTING:
        if (self->startup_timer_s > 0U) {
            self->startup_timer_s--;
        }
        if (self->startup_timer_s == 0U) {
            self->state = UNIT_STATE_RUNNING;
            self->fan_state = HVAC_FAN_STATE_COMPRESSOR_RUNNING;
        }
        break;

    case UNIT_STATE_STOPPING:
        if (self->fan_delay_timer_s > 0U) {
            self->fan_delay_timer_s--;
        }
        if (self->fan_delay_timer_s == 0U) {
            self->fan_state = self->enable_fan_delay ? HVAC_FAN_STATE_SMART_IDLE_ON : HVAC_FAN_STATE_DISABLED;
            self->state = UNIT_STATE_OFF;
        }
        break;

    case UNIT_STATE_BLOCKED_ALARM:
        if (self->retry_timer_s > 0U) {
            self->retry_timer_s--;
        }
        if (self->retry_timer_s == 0U) {
            if (!has_active_alarm(self)) {
                self->state = UNIT_STATE_OFF;
                self->fan_state = self->enable_fan_delay ? HVAC_FAN_STATE_SMART_IDLE_ON : HVAC_FAN_STATE_DISABLED;
            }
        }
        break;

    case UNIT_STATE_OFF:
    case UNIT_STATE_RUNNING:
    case UNIT_STATE_FAULT:
    case UNIT_STATE_BYPASS:
    default:
        break;
    }
}

bool hvac_unit_entity_request_start(hvac_unit_entity_t *self)
{
    if (self == NULL) {
        return false;
    }
    return hvac_unit_entity_request_start_staggered(self, self->startup_delay_s);
}

bool hvac_unit_entity_request_start_staggered(hvac_unit_entity_t *self, uint16_t delay_s)
{
    if (self == NULL) {
        return false;
    }

    if (self->state == UNIT_STATE_FAULT || self->state == UNIT_STATE_BLOCKED_ALARM) {
        return false;
    }

    if (has_active_alarm(self)) {
        return false;
    }

    if (self->state == UNIT_STATE_BYPASS) {
        return false;
    }

    if (self->state == UNIT_STATE_RUNNING || self->state == UNIT_STATE_STARTING) {
        return true;
    }

    self->fan_delay_timer_s = 0U;

    if (delay_s == 0U) {
        self->state = UNIT_STATE_RUNNING;
        self->fan_state = HVAC_FAN_STATE_COMPRESSOR_RUNNING;
        self->startup_timer_s = 0U;
    } else {
        self->state = UNIT_STATE_STARTING;
        self->fan_state = self->enable_fan_delay ? HVAC_FAN_STATE_SMART_IDLE_ON : HVAC_FAN_STATE_DISABLED;
        self->startup_timer_s = delay_s;
    }

    return true;
}

void hvac_unit_entity_request_stop(hvac_unit_entity_t *self, bool enable_fan_delay)
{
    if (self == NULL) {
        return;
    }

    self->enable_fan_delay = enable_fan_delay;

    if (self->state == UNIT_STATE_BYPASS) {
        self->state = UNIT_STATE_OFF;
        self->fan_state = enable_fan_delay ? HVAC_FAN_STATE_SMART_IDLE_ON : HVAC_FAN_STATE_DISABLED;
        self->startup_timer_s = 0U;
        self->fan_delay_timer_s = 0U;
        return;
    }

    if (self->state == UNIT_STATE_FAULT || self->state == UNIT_STATE_BLOCKED_ALARM) {
        self->fan_state = enable_fan_delay ? HVAC_FAN_STATE_SMART_IDLE_ON : HVAC_FAN_STATE_DISABLED;
        return;
    }

    if (!enable_fan_delay) {
        self->state = UNIT_STATE_OFF;
        self->fan_state = HVAC_FAN_STATE_DISABLED;
        self->startup_timer_s = 0U;
        self->fan_delay_timer_s = 0U;
        return;
    }

    if (self->state == UNIT_STATE_RUNNING) {
        if (self->fan_delay_s > 0U) {
            self->state = UNIT_STATE_STOPPING;
            self->fan_state = HVAC_FAN_STATE_WAIT_DELAY;
            self->fan_delay_timer_s = self->fan_delay_s;
        } else {
            self->state = UNIT_STATE_OFF;
            self->fan_state = HVAC_FAN_STATE_SMART_IDLE_ON;
            self->fan_delay_timer_s = 0U;
        }
        self->startup_timer_s = 0U;
    } else if (self->state == UNIT_STATE_STARTING) {
        /* Compressor never turned on: return directly to OFF without post-purge delay */
        self->state = UNIT_STATE_OFF;
        self->fan_state = HVAC_FAN_STATE_SMART_IDLE_ON;
        self->startup_timer_s = 0U;
        self->fan_delay_timer_s = 0U;
    } else if (self->state == UNIT_STATE_OFF) {
        self->fan_state = HVAC_FAN_STATE_SMART_IDLE_ON;
        self->fan_delay_timer_s = 0U;
    }
}

void hvac_unit_entity_set_bypass(hvac_unit_entity_t *self)
{
    if (self == NULL) {
        return;
    }

    self->state = UNIT_STATE_BYPASS;
    self->fan_state = HVAC_FAN_STATE_DISABLED;
    self->startup_timer_s = 0U;
    self->fan_delay_timer_s = 0U;
}

void hvac_unit_entity_set_manual(hvac_unit_entity_t *self, bool turn_on)
{
    if (self == NULL) {
        return;
    }

    if (turn_on) {
        if (self->state != UNIT_STATE_FAULT && self->state != UNIT_STATE_BLOCKED_ALARM) {
            self->state = UNIT_STATE_RUNNING;
            self->fan_state = HVAC_FAN_STATE_COMPRESSOR_RUNNING;
            self->startup_timer_s = 0U;
            self->fan_delay_timer_s = 0U;
        }
    } else {
        self->state = UNIT_STATE_OFF;
        self->fan_state = self->enable_fan_delay ? HVAC_FAN_STATE_SMART_IDLE_ON : HVAC_FAN_STATE_DISABLED;
        self->startup_timer_s = 0U;
        self->fan_delay_timer_s = 0U;
    }
}

hvac_tri_state_t hvac_unit_entity_get_tri_state(const hvac_unit_entity_t *self)
{
    if (self == NULL) {
        return HVAC_TRI_STATE_OFF;
    }

    hvac_tri_state_t tri;
    tri.relay_aa = (self->state == UNIT_STATE_RUNNING);
    tri.relay_sm = (self->state != UNIT_STATE_BYPASS);

    /*
     * relay_g (ventilador, relé NC: G=1 apaga el ventilador, G=0 lo enciende):
     * - Fuego: G=1 siempre (fail-safe, nunca ventilar en FIRE).
     * - Compresor en marcha o post-purga: G=0 (ventilador encendido).
     * - Sin enable_fan_delay: G=0 salvo fuego.
     * - Con enable_fan_delay en OFF/STARTING/BLOCKED_ALARM/FAULT: G=1
     *   (ventilador apagado fuera de operación de compresor).
     * - Bypass: G=0 (relés desenergizados).
     */
    if (self->state == UNIT_STATE_BYPASS) {
        tri.relay_g = false;
    } else if (self->fan_state == HVAC_FAN_STATE_FIRE) {
        tri.relay_g = true;
    } else if (self->state == UNIT_STATE_RUNNING ||
               self->state == UNIT_STATE_STOPPING) {
        tri.relay_g = false;
    } else if (!self->enable_fan_delay) {
        tri.relay_g = false;
    } else {
        tri.relay_g = true;
    }

    return tri;
}

void hvac_unit_entity_get_relays(const hvac_unit_entity_t *self,
                                 bool *relay_aa,
                                 bool *relay_sm,
                                 bool *relay_g)
{
    hvac_tri_state_t tri = hvac_unit_entity_get_tri_state(self);

    if (relay_aa != NULL) {
        *relay_aa = tri.relay_aa;
    }

    if (relay_sm != NULL) {
        *relay_sm = tri.relay_sm;
    }

    if (relay_g != NULL) {
        *relay_g = tri.relay_g;
    }
}
