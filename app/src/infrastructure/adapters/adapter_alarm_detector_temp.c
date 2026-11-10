/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_alarm_detector_temp.c
 * @brief Implementation of Group Temperature Alarm Ingestion Detector.
 */

#include "infrastructure/adapters/adapter_alarm_detector_temp.h"
#include <errno.h>

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

int adapter_alarm_detector_temp_set_group_config(adapter_alarm_detector_temp_t *self,
                                                 uint8_t group_idx,
                                                 uint8_t min_id,
                                                 uint8_t max_id,
                                                 float t_low_c,
                                                 float t_high_c,
                                                 float hyst_c)
{
    if (self == NULL || group_idx >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    if (min_id > 0U && max_id < min_id) {
        return -EINVAL;
    }

    bool was_enabled = self->groups[group_idx].enabled;
    bool now_enabled = (min_id > 0U && max_id >= min_id);

    self->groups[group_idx].min_id = min_id;
    self->groups[group_idx].max_id = max_id;
    self->groups[group_idx].t_low_c = t_low_c;
    self->groups[group_idx].t_high_c = t_high_c;
    self->groups[group_idx].hyst_c = (hyst_c > 0.0f) ? hyst_c : 1.0f;
    self->groups[group_idx].enabled = now_enabled;

    for (uint8_t i = 0; i < 16; i++) {
        alarm_eval_temp_low_init(&self->eval_low[group_idx][i], t_low_c, self->groups[group_idx].hyst_c);
        alarm_eval_temp_high_init(&self->eval_high[group_idx][i], t_high_c, self->groups[group_idx].hyst_c);

        if (!now_enabled && was_enabled) {
            if (self->last_is_low[group_idx][i]) {
                self->last_is_low[group_idx][i] = false;
                if (self->manager != NULL) {
                    (void)ao_alarm_manager_post_source_condition(self->manager,
                                                                 s_temp_low_ids[group_idx],
                                                                 i, false, 0.0f);
                }
            }
            if (self->last_is_high[group_idx][i]) {
                self->last_is_high[group_idx][i] = false;
                if (self->manager != NULL) {
                    (void)ao_alarm_manager_post_source_condition(self->manager,
                                                                 s_temp_high_ids[group_idx],
                                                                 i, false, 0.0f);
                }
            }
        }
    }

    if (!now_enabled && was_enabled && self->group_fault_active[group_idx]) {
        self->group_fault_active[group_idx] = false;
        if (self->manager != NULL) {
            (void)ao_alarm_manager_post_condition(self->manager,
                                                 s_sensor_fault_ids[group_idx],
                                                 false, 0.0f);
        }
    }

    return 0;
}

int adapter_alarm_detector_temp_set_group_range(adapter_alarm_detector_temp_t *self,
                                                uint8_t group_idx,
                                                uint8_t min_id,
                                                uint8_t max_id)
{
    if (self == NULL || group_idx >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    float t_low = self->groups[group_idx].t_low_c;
    float t_high = self->groups[group_idx].t_high_c;
    float hyst = self->groups[group_idx].hyst_c;
    return adapter_alarm_detector_temp_set_group_config(self, group_idx, min_id, max_id, t_low, t_high, hyst);
}

int adapter_alarm_detector_temp_set_group_from_hvac(adapter_alarm_detector_temp_t *self,
                                                    uint8_t group_idx,
                                                    const config_hvac_group_t *grp)
{
    if (self == NULL || group_idx >= HVAC_MAX_GROUPS || grp == NULL) {
        return -EINVAL;
    }

    /* High temp alarm setpoint directly from group temp_alarm / temp boost */
    float t_high = grp->temp_alarm;
    /* Low temp alarm setpoint from persistent group configuration (default 15.0C) */
    float t_low = (grp->temp_low_alarm_c != 0) ? (float)grp->temp_low_alarm_c : 15.0f;
    float hyst = 1.0f;

    return adapter_alarm_detector_temp_set_group_config(self, group_idx,
                                                        grp->sensor_start,
                                                        grp->sensor_end,
                                                        t_low, t_high, hyst);
}

int adapter_alarm_detector_temp_apply_hvac_config(adapter_alarm_detector_temp_t *self,
                                                  const config_hvac_t *hvac_cfg)
{
    if (self == NULL) {
        return -EINVAL;
    }

    for (uint8_t g = 0; g < HVAC_MAX_GROUPS; g++) {
        if (hvac_cfg != NULL && g < hvac_cfg->total_groups) {
            (void)adapter_alarm_detector_temp_set_group_from_hvac(self, g, &hvac_cfg->groups[g]);
        } else {
            /* Group disabled / unconfigured */
            (void)adapter_alarm_detector_temp_set_group_config(self, g, 0U, 0U, 15.0f, 28.0f, 1.0f);
        }
    }
    return 0;
}

void adapter_alarm_detector_temp_init(adapter_alarm_detector_temp_t *self,
                                      ao_alarm_manager_t *mgr)
{
    if (self == NULL) {
        return;
    }

    (void)memset(self, 0, sizeof(*self));
    self->manager = mgr;
}

void adapter_alarm_detector_temp_process_snapshot(adapter_alarm_detector_temp_t *self,
                                                  const sensor_snapshot_msg_t *snapshot)
{
    if (self == NULL || self->manager == NULL || snapshot == NULL) {
        return;
    }

    for (size_t g = 0; g < HVAC_MAX_GROUPS; g++) {
        const adapter_temp_group_config_t *grp = &self->groups[g];
        if (!grp->enabled) {
            continue;
        }

        bool group_has_fault = false;

        for (uint8_t i = 0; i < 16; i++) {
            uint8_t sensor_id = (uint8_t)(i + 1U); /* 1-based */
            if (sensor_id < grp->min_id || sensor_id > grp->max_id) {
                continue; /* Probe not assigned to this group */
            }

            bool is_valid = ((snapshot->valid_mask & (1ULL << i)) != 0ULL);

            if (!is_valid) {
                /* Disconnected sensor: do NOT clear low/high condition to avoid false recovery.
                 * Assert fault on this group. */
                group_has_fault = true;
                continue;
            }

            float temp = snapshot->temperatures[i];

            /* Evaluate Low Temperature */
            bool is_low = alarm_eval_temp_low_update(&self->eval_low[g][i], temp);
            if (is_low != self->last_is_low[g][i]) {
                self->last_is_low[g][i] = is_low;
                (void)ao_alarm_manager_post_source_condition(self->manager,
                                                             s_temp_low_ids[g],
                                                             i, is_low, temp);
            }

            /* Evaluate High Temperature */
            bool is_high = alarm_eval_temp_high_update(&self->eval_high[g][i], temp);
            if (is_high != self->last_is_high[g][i]) {
                self->last_is_high[g][i] = is_high;
                (void)ao_alarm_manager_post_source_condition(self->manager,
                                                             s_temp_high_ids[g],
                                                             i, is_high, temp);
            }
        }

        /* Post Group Sensor Fault */
        if (group_has_fault != self->group_fault_active[g]) {
            self->group_fault_active[g] = group_has_fault;
            (void)ao_alarm_manager_post_condition(self->manager,
                                                 s_sensor_fault_ids[g],
                                                 group_has_fault,
                                                 group_has_fault ? 1.0f : 0.0f);
        }
    }
}
