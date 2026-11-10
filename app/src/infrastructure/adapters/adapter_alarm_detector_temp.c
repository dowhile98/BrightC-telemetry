/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_alarm_detector_temp.c
 * @brief Implementation of Group Temperature Alarm Ingestion Detector.
 */

#include "infrastructure/adapters/adapter_alarm_detector_temp.h"

static const uint16_t s_temp_low_ids[HVAC_MAX_GROUPS] = {
    ALARM_ID_GROUP1_TEMP_LOW,
    ALARM_ID_GROUP2_TEMP_LOW,
    ALARM_ID_GROUP3_TEMP_LOW,
    ALARM_ID_GROUP4_TEMP_LOW
};

static const uint16_t s_temp_high_ids[HVAC_MAX_GROUPS] = {
    ALARM_ID_GROUP1_TEMP_HIGH,
    ALARM_ID_GROUP2_TEMP_HIGH,
    ALARM_ID_GROUP3_TEMP_HIGH,
    ALARM_ID_GROUP4_TEMP_HIGH
};

static const uint16_t s_sensor_fault_ids[HVAC_MAX_GROUPS] = {
    ALARM_ID_SENSOR_GROUP1_FAULT,
    ALARM_ID_SENSOR_GROUP2_FAULT,
    ALARM_ID_SENSOR_GROUP3_FAULT,
    ALARM_ID_SENSOR_GROUP4_FAULT
};

void adapter_alarm_detector_temp_init(adapter_alarm_detector_temp_t *self,
                                      ao_alarm_manager_t *mgr,
                                      const uint16_t *group_masks,
                                      float t_low_c,
                                      float t_high_c,
                                      float hyst_c)
{
    if (self == NULL) {
        return;
    }

    self->manager = mgr;
    for (size_t g = 0; g < HVAC_MAX_GROUPS; g++) {
        if (group_masks != NULL) {
            self->group_sensor_masks[g] = group_masks[g];
        } else {
            self->group_sensor_masks[g] = (g == 0) ? 0x000FU : 0x0000U;
        }
        self->group_fault_active[g] = false;
    }

    for (size_t i = 0; i < 16; i++) {
        alarm_eval_temp_low_init(&self->eval_low[i], t_low_c, hyst_c);
        alarm_eval_temp_high_init(&self->eval_high[i], t_high_c, hyst_c);
        self->last_is_low[i] = false;
        self->last_is_high[i] = false;
    }
}

void adapter_alarm_detector_temp_process_snapshot(adapter_alarm_detector_temp_t *self,
                                                  const sensor_snapshot_msg_t *snapshot)
{
    if (self == NULL || self->manager == NULL || snapshot == NULL) {
        return;
    }

    bool any_group_fault[HVAC_MAX_GROUPS] = { false };

    for (uint8_t i = 0; i < 16; i++) {
        uint16_t bit = (1U << i);
        bool assigned = false;

        for (size_t g = 0; g < HVAC_MAX_GROUPS; g++) {
            if ((self->group_sensor_masks[g] & bit) != 0U) {
                assigned = true;
                break;
            }
        }

        if (!assigned) {
            continue; /* Sensor probe not assigned to any thermal group */
        }

        bool is_valid = ((snapshot->valid_mask & (1ULL << i)) != 0ULL);

        if (!is_valid) {
            /* Disconnected sensor: do NOT clear low/high condition to avoid false recovery.
             * Assert fault on all groups assigned to this probe. */
            for (size_t g = 0; g < HVAC_MAX_GROUPS; g++) {
                if ((self->group_sensor_masks[g] & bit) != 0U) {
                    any_group_fault[g] = true;
                }
            }
            continue;
        }

        float temp = snapshot->temperatures[i];

        /* Evaluate Low Temperature */
        bool is_low = alarm_eval_temp_low_update(&self->eval_low[i], temp);
        if (is_low != self->last_is_low[i]) {
            self->last_is_low[i] = is_low;
            for (size_t g = 0; g < HVAC_MAX_GROUPS; g++) {
                if ((self->group_sensor_masks[g] & bit) != 0U) {
                    (void)ao_alarm_manager_post_source_condition(self->manager,
                                                                 s_temp_low_ids[g],
                                                                 i, is_low, temp);
                }
            }
        }

        /* Evaluate High Temperature */
        bool is_high = alarm_eval_temp_high_update(&self->eval_high[i], temp);
        if (is_high != self->last_is_high[i]) {
            self->last_is_high[i] = is_high;
            for (size_t g = 0; g < HVAC_MAX_GROUPS; g++) {
                if ((self->group_sensor_masks[g] & bit) != 0U) {
                    (void)ao_alarm_manager_post_source_condition(self->manager,
                                                                 s_temp_high_ids[g],
                                                                 i, is_high, temp);
                }
            }
        }
    }

    /* Update Sensor Fault Alarms per group on state transition */
    for (size_t g = 0; g < HVAC_MAX_GROUPS; g++) {
        if (self->group_sensor_masks[g] != 0U && any_group_fault[g] != self->group_fault_active[g]) {
            self->group_fault_active[g] = any_group_fault[g];
            (void)ao_alarm_manager_post_condition(self->manager,
                                                  s_sensor_fault_ids[g],
                                                  any_group_fault[g], 0.0f);
        }
    }
}
