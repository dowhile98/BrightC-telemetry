/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_alarm_detector_di.c
 * @brief Implementation of Digital Input Contact Alarm Detector.
 */

#include "infrastructure/adapters/adapter_alarm_detector_di.h"

uint16_t adapter_alarm_detector_di_role_to_alarm(di_role_t role)
{
    switch (role) {
    /* AC1 (Unit 0) */
    case DI_ROLE_UNIT_0_ALARM_HP:   return ALARM_ID_AC1_HIGH_PRESSURE;
    case DI_ROLE_UNIT_0_ALARM_LP:   return ALARM_ID_AC1_LOW_PRESSURE;
    case DI_ROLE_UNIT_0_ALARM_FLOW: return ALARM_ID_AC1_AIRFLOW_FAULT;
    case DI_ROLE_UNIT_0_ALARM_VOLT: return ALARM_ID_AC1_VOLTAGE_FAULT;

    /* AC2 (Unit 1) */
    case DI_ROLE_UNIT_1_ALARM_HP:   return ALARM_ID_AC2_HIGH_PRESSURE;
    case DI_ROLE_UNIT_1_ALARM_LP:   return ALARM_ID_AC2_LOW_PRESSURE;
    case DI_ROLE_UNIT_1_ALARM_FLOW: return ALARM_ID_AC2_AIRFLOW_FAULT;
    case DI_ROLE_UNIT_1_ALARM_VOLT: return ALARM_ID_AC2_VOLTAGE_FAULT;

    /* AC3 (Unit 2) */
    case DI_ROLE_UNIT_2_ALARM_HP:   return ALARM_ID_AC3_HIGH_PRESSURE;
    case DI_ROLE_UNIT_2_ALARM_LP:   return ALARM_ID_AC3_LOW_PRESSURE;
    case DI_ROLE_UNIT_2_ALARM_FLOW: return ALARM_ID_AC3_AIRFLOW_FAULT;
    case DI_ROLE_UNIT_2_ALARM_VOLT: return ALARM_ID_AC3_VOLTAGE_FAULT;

    /* AC4 (Unit 3) */
    case DI_ROLE_UNIT_3_ALARM_HP:   return ALARM_ID_AC4_HIGH_PRESSURE;
    case DI_ROLE_UNIT_3_ALARM_LP:   return ALARM_ID_AC4_LOW_PRESSURE;
    case DI_ROLE_UNIT_3_ALARM_FLOW: return ALARM_ID_AC4_AIRFLOW_FAULT;
    case DI_ROLE_UNIT_3_ALARM_VOLT: return ALARM_ID_AC4_VOLTAGE_FAULT;

    /* AC5 (Unit 4) */
    case DI_ROLE_UNIT_4_ALARM_HP:   return ALARM_ID_AC5_HIGH_PRESSURE;
    case DI_ROLE_UNIT_4_ALARM_LP:   return ALARM_ID_AC5_LOW_PRESSURE;
    case DI_ROLE_UNIT_4_ALARM_FLOW: return ALARM_ID_AC5_AIRFLOW_FAULT;
    case DI_ROLE_UNIT_4_ALARM_VOLT: return ALARM_ID_AC5_VOLTAGE_FAULT;

    /* AC6 (Unit 5) */
    case DI_ROLE_UNIT_5_ALARM_HP:   return ALARM_ID_AC6_HIGH_PRESSURE;
    case DI_ROLE_UNIT_5_ALARM_LP:   return ALARM_ID_AC6_LOW_PRESSURE;
    case DI_ROLE_UNIT_5_ALARM_FLOW: return ALARM_ID_AC6_AIRFLOW_FAULT;
    case DI_ROLE_UNIT_5_ALARM_VOLT: return ALARM_ID_AC6_VOLTAGE_FAULT;

    /* AC7 (Unit 6) */
    case DI_ROLE_UNIT_6_ALARM_HP:   return ALARM_ID_AC7_HIGH_PRESSURE;
    case DI_ROLE_UNIT_6_ALARM_LP:   return ALARM_ID_AC7_LOW_PRESSURE;
    case DI_ROLE_UNIT_6_ALARM_FLOW: return ALARM_ID_AC7_AIRFLOW_FAULT;
    case DI_ROLE_UNIT_6_ALARM_VOLT: return ALARM_ID_AC7_VOLTAGE_FAULT;

    /* AC8 (Unit 7) */
    case DI_ROLE_UNIT_7_ALARM_HP:   return ALARM_ID_AC8_HIGH_PRESSURE;
    case DI_ROLE_UNIT_7_ALARM_LP:   return ALARM_ID_AC8_LOW_PRESSURE;
    case DI_ROLE_UNIT_7_ALARM_FLOW: return ALARM_ID_AC8_AIRFLOW_FAULT;
    case DI_ROLE_UNIT_7_ALARM_VOLT: return ALARM_ID_AC8_VOLTAGE_FAULT;

    /* Facility Contacts */
    case DI_ROLE_SMOKE_FIRE:        return ALARM_ID_FACILITY_FIRE;
    case DI_ROLE_DOOR_OPEN:         return ALARM_ID_FACILITY_DOOR_OPEN;
    case DI_ROLE_WATER_LEAK:        return ALARM_ID_FACILITY_WATER_LEAK;
    case DI_ROLE_SURGE_PROTECTOR:   return ALARM_ID_FACILITY_SURGE_PROT;
    case DI_ROLE_PHASE_MONITOR:     return ALARM_ID_FACILITY_PHASE_MON;

    default:
        return ALARM_ID_NONE;
    }
}

void adapter_alarm_detector_di_init(adapter_alarm_detector_di_t *self, ao_alarm_manager_t *mgr)
{
    if (self != NULL) {
        self->manager = mgr;
    }
}

void adapter_alarm_detector_di_process_transition(adapter_alarm_detector_di_t *self,
                                                  const di_transition_event_t *event)
{
    if (self == NULL || self->manager == NULL || event == NULL) {
        return;
    }

    uint16_t alarm_id = adapter_alarm_detector_di_role_to_alarm(event->role);
    if (alarm_id == ALARM_ID_NONE) {
        return;
    }

    bool is_asserted = (event->new_state == DI_STATE_ACTIVE);
    (void)ao_alarm_manager_post_condition(self->manager, alarm_id, is_asserted, 0.0f);

    /* Propagate to Consolidated Summary Alarm for HVAC units (AC1..AC8) */
    if (event->role >= DI_ROLE_UNIT_0_ALARM_HP && event->role <= DI_ROLE_UNIT_7_ALARM_VOLT) {
        uint8_t unit_idx = (uint8_t)((event->role - DI_ROLE_UNIT_0_ALARM_HP) / 4U);
        uint8_t contact_src = (uint8_t)((event->role - DI_ROLE_UNIT_0_ALARM_HP) % 4U);
        uint16_t summary_id = (uint16_t)(ALARM_ID_AC1_SUMMARY_FAULT + unit_idx);
        (void)ao_alarm_manager_post_source_condition(self->manager, summary_id, contact_src, is_asserted, 0.0f);
    }
}
