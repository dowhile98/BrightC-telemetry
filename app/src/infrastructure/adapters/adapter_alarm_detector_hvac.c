/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_alarm_detector_hvac.c
 * @brief Implementation of HVAC Bypass, Cooling Boost, and Unit Lockout Alarm Detector.
 */

#include "infrastructure/adapters/adapter_alarm_detector_hvac.h"

void adapter_alarm_detector_hvac_init(adapter_alarm_detector_hvac_t *self, ao_alarm_manager_t *mgr)
{
    if (self == NULL) {
        return;
    }
    self->manager = mgr;
    for (size_t g = 0; g < HVAC_MAX_GROUPS; g++) {
        self->is_group_bypass_active[g] = false;
        self->is_group_boost_active[g] = false;
    }
    for (size_t i = 0; i < HVAC_MAX_UNITS_TOTAL; i++) {
        self->is_unit_lockout[i] = false;
    }
}

void adapter_alarm_detector_hvac_process_status(adapter_alarm_detector_hvac_t *self,
                                                const hvac_status_msg_t *status)
{
    if (self == NULL || self->manager == NULL || status == NULL) {
        return;
    }

    /* 1. Evaluate Bypass State across all groups */
    static const uint16_t bypass_alarm_ids[HVAC_MAX_GROUPS] = {
        ALARM_ID_HVAC_G1_BYPASS_ACTIVE,
        ALARM_ID_HVAC_G2_BYPASS_ACTIVE,
        ALARM_ID_HVAC_G3_BYPASS_ACTIVE,
        ALARM_ID_HVAC_G4_BYPASS_ACTIVE
    };

    for (size_t g = 0; g < HVAC_MAX_GROUPS; g++) {
        bool bypass_now = (status->group_state[g] == GROUP_STATE_BYPASS || status->bypass_causes[g] != 0U);
        if (bypass_now != self->is_group_bypass_active[g]) {
            self->is_group_bypass_active[g] = bypass_now;
            (void)ao_alarm_manager_post_condition(self->manager,
                                                  bypass_alarm_ids[g],
                                                  bypass_now, 0.0f);
        }
    }

    /* 2. Evaluate Cooling Boost State across all groups */
    static const uint16_t boost_alarm_ids[HVAC_MAX_GROUPS] = {
        ALARM_ID_GROUP1_TEMP_BOOST,
        ALARM_ID_GROUP2_TEMP_BOOST,
        ALARM_ID_GROUP3_TEMP_BOOST,
        ALARM_ID_GROUP4_TEMP_BOOST
    };

    for (size_t g = 0; g < HVAC_MAX_GROUPS; g++) {
        bool boost_now = (status->group_state[g] == GROUP_STATE_COOL_BOOST);
        if (boost_now != self->is_group_boost_active[g]) {
            self->is_group_boost_active[g] = boost_now;
            (void)ao_alarm_manager_post_condition(self->manager,
                                                  boost_alarm_ids[g],
                                                  boost_now, 0.0f);
        }
    }

    /* 3. Evaluate AC Units 1..8 Lockouts */
    static const uint16_t lockout_alarm_ids[HVAC_MAX_UNITS_TOTAL] = {
        ALARM_ID_HVAC_UNIT1_LOCKOUT,
        ALARM_ID_HVAC_UNIT2_LOCKOUT,
        ALARM_ID_HVAC_UNIT3_LOCKOUT,
        ALARM_ID_HVAC_UNIT4_LOCKOUT,
        ALARM_ID_HVAC_UNIT5_LOCKOUT,
        ALARM_ID_HVAC_UNIT6_LOCKOUT,
        ALARM_ID_HVAC_UNIT7_LOCKOUT,
        ALARM_ID_HVAC_UNIT8_LOCKOUT
    };

    for (size_t i = 0; i < HVAC_MAX_UNITS_TOTAL; i++) {
        bool locked = (status->unit_state[i] == UNIT_STATE_BLOCKED_ALARM);
        if (locked != self->is_unit_lockout[i]) {
            self->is_unit_lockout[i] = locked;
            (void)ao_alarm_manager_post_condition(self->manager,
                                                  lockout_alarm_ids[i],
                                                  locked, 0.0f);
        }
    }
}
