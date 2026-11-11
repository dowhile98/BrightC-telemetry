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

static inline uint8_t sensor_to_source_idx(const adapter_temp_group_config_t *grp, uint8_t flat_idx)
{
    uint8_t sensor_id = (uint8_t)(flat_idx + 1U);
    if (grp != NULL && grp->min_id > 0U && sensor_id >= grp->min_id) {
        return (uint8_t)((sensor_id - grp->min_id) & 0x1FU);
    }
    return (uint8_t)(flat_idx & 0x1FU);
}

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
    if (min_id > 0U && (max_id < min_id || max_id > ADAPTER_TEMP_MAX_SENSORS)) {
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

    for (uint8_t src_idx = 0; src_idx < ADAPTER_TEMP_MAX_GROUP_SOURCES; src_idx++) {
        alarm_eval_temp_low_init(&self->eval_low[group_idx][src_idx], t_low_c, self->groups[group_idx].hyst_c);
        alarm_eval_temp_high_init(&self->eval_high[group_idx][src_idx], t_high_c, self->groups[group_idx].hyst_c);

        if (!now_enabled && was_enabled) {
            if (self->last_is_low[group_idx][src_idx]) {
                self->last_is_low[group_idx][src_idx] = false;
                if (self->manager != NULL) {
                    (void)ao_alarm_manager_post_source_condition(self->manager,
                                                                 s_temp_low_ids[group_idx],
                                                                 src_idx, false, 0.0f);
                }
            }
            if (self->last_is_high[group_idx][src_idx]) {
                self->last_is_high[group_idx][src_idx] = false;
                if (self->manager != NULL) {
                    (void)ao_alarm_manager_post_source_condition(self->manager,
                                                                 s_temp_high_ids[group_idx],
                                                                 src_idx, false, 0.0f);
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

        uint8_t assigned_sensors = 0U;
        uint8_t valid_sensors = 0U;

        for (uint8_t i = 0; i < ADAPTER_TEMP_MAX_SENSORS; i++) {
            uint8_t sensor_id = (uint8_t)(i + 1U); /* 1-based */
            if (sensor_id < grp->min_id || sensor_id > grp->max_id) {
                continue;
            }
            if ((uint8_t)(sensor_id - grp->min_id) >= ADAPTER_TEMP_MAX_GROUP_SOURCES) {
                continue;
            }
            assigned_sensors++;
            if ((snapshot->valid_mask & (1ULL << i)) != 0ULL) {
                valid_sensors++;
            }
        }

        /* Group sensor fault is asserted only when ALL assigned sensors in the group are invalid
         * (consistent with HVAC control BYPASS_CAUSE_SENSOR_FAULT and BrightC-V3). */
        bool group_has_fault = (assigned_sensors > 0U && valid_sensors == 0U);

        for (uint8_t i = 0; i < ADAPTER_TEMP_MAX_SENSORS; i++) {
            uint8_t sensor_id = (uint8_t)(i + 1U); /* 1-based */
            if (sensor_id < grp->min_id || sensor_id > grp->max_id) {
                continue; /* Probe not assigned to this group */
            }
            if ((uint8_t)(sensor_id - grp->min_id) >= ADAPTER_TEMP_MAX_GROUP_SOURCES) {
                continue;
            }

            uint8_t src_idx = sensor_to_source_idx(grp, i);
            bool is_valid = ((snapshot->valid_mask & (1ULL << i)) != 0ULL);

            if (!is_valid) {
                if (valid_sensors > 0U) {
                    /* Other probes in the group are healthy: clear any stale thermal alarm
                     * from this disconnected/unpopulated channel and reset its evaluator. */
                    alarm_eval_temp_low_init(&self->eval_low[g][src_idx], grp->t_low_c, grp->hyst_c);
                    alarm_eval_temp_high_init(&self->eval_high[g][src_idx], grp->t_high_c, grp->hyst_c);
                    if (self->last_is_low[g][src_idx]) {
                        self->last_is_low[g][src_idx] = false;
                        (void)ao_alarm_manager_post_source_condition(self->manager,
                                                                     s_temp_low_ids[g],
                                                                     src_idx, false, 0.0f);
                    }
                    if (self->last_is_high[g][src_idx]) {
                        self->last_is_high[g][src_idx] = false;
                        (void)ao_alarm_manager_post_source_condition(self->manager,
                                                                     s_temp_high_ids[g],
                                                                     src_idx, false, 0.0f);
                    }
                }
                /* If valid_sensors == 0 (total sensor loss for the group), retain existing
                 * thermal alarm state so sensor loss cannot mask an active thermal alarm. */
                continue;
            }

            float temp = snapshot->temperatures[i];

            /* Evaluate Low Temperature */
            bool is_low = alarm_eval_temp_low_update(&self->eval_low[g][src_idx], temp);
            if (is_low != self->last_is_low[g][src_idx]) {
                self->last_is_low[g][src_idx] = is_low;
                (void)ao_alarm_manager_post_source_condition(self->manager,
                                                             s_temp_low_ids[g],
                                                             src_idx, is_low, temp);
            }

            /* Evaluate High Temperature */
            bool is_high = alarm_eval_temp_high_update(&self->eval_high[g][src_idx], temp);
            if (is_high != self->last_is_high[g][src_idx]) {
                self->last_is_high[g][src_idx] = is_high;
                (void)ao_alarm_manager_post_source_condition(self->manager,
                                                             s_temp_high_ids[g],
                                                             src_idx, is_high, temp);
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
