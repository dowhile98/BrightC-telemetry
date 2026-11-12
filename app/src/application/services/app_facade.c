/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file app_facade.c
 * @brief Implementation of Application Service Facade.
 */

#include "application/services/app_facade.h"
#include "application/zbus/sensor_channels.h"
#include "application/zbus/digital_input_channels.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "infrastructure/di/composition_root.h"
#include "interfaces/i_thermal_energy.h"
#include "interfaces/i_rtc.h"
#include "domain/time/rtc_time_calc.h"
#include "common/config_envelope.h"
#include "common/app_defaults.h"
#include "common/modbus_device_table.h"
#include "interfaces/i_modbus_tcp_server.h"
#include "transducer_rtd.h"

LOG_MODULE_REGISTER(app_facade, LOG_LEVEL_INF);

/* Reference to application composition root container */
static app_container_t *s_app = NULL;

/* Persistent storage statistics counters */
static storage_stats_t s_storage_stats = {0};

/* Sensor calibration offsets (in 0.1°C units) */
static int16_t s_sensor_cal_offsets[21] = {0};



/* =========================================================================
 * Helper Functions
 * ========================================================================= */

static const char *role_to_name(di_role_t role)
{
    switch (role) {
    case DI_ROLE_FAN_1_AUX: return "Fan 1 Run Aux";
    case DI_ROLE_FAN_2_AUX: return "Fan 2 Run Aux";
    case DI_ROLE_FAN_3_AUX: return "Fan 3 Run Aux";
    case DI_ROLE_FAN_4_AUX: return "Fan 4 Run Aux";
    case DI_ROLE_FAN_5_AUX: return "Fan 5 Run Aux";
    case DI_ROLE_FAN_6_AUX: return "Fan 6 Run Aux";
    case DI_ROLE_FAN_7_AUX: return "Fan 7 Run Aux";
    case DI_ROLE_FAN_8_AUX: return "Fan 8 Run Aux";
    case DI_ROLE_COMPRESSOR_1_AUX: return "Comp 1 Run Aux";
    case DI_ROLE_COMPRESSOR_2_AUX: return "Comp 2 Run Aux";
    case DI_ROLE_COMPRESSOR_3_AUX: return "Comp 3 Run Aux";
    case DI_ROLE_COMPRESSOR_4_AUX: return "Comp 4 Run Aux";
    case DI_ROLE_COMPRESSOR_5_AUX: return "Comp 5 Run Aux";
    case DI_ROLE_COMPRESSOR_6_AUX: return "Comp 6 Run Aux";
    case DI_ROLE_COMPRESSOR_7_AUX: return "Comp 7 Run Aux";
    case DI_ROLE_COMPRESSOR_8_AUX: return "Comp 8 Run Aux";
    case DI_ROLE_HIGH_PRESSURE_1: return "High Pressure 1";
    case DI_ROLE_HIGH_PRESSURE_2: return "High Pressure 2";
    case DI_ROLE_LOW_PRESSURE_1: return "Low Pressure 1";
    case DI_ROLE_LOW_PRESSURE_2: return "Low Pressure 2";
    case DI_ROLE_THERMAL_OVERLOAD_FAN: return "Fan Thermal Overload";
    case DI_ROLE_THERMAL_OVERLOAD_COMP: return "Comp Thermal Overload";
    case DI_ROLE_WATER_LEAK: return "Water Leak Inundation";
    case DI_ROLE_SMOKE_FIRE: return "Smoke/Fire Interlock";
    case DI_ROLE_PHASE_MONITOR: return "Phase Monitor Fault";
    case DI_ROLE_SURGE_PROTECTOR: return "Surge Protector (DPS)";
    case DI_ROLE_DOOR_OPEN: return "Containment Door Open";
    case DI_ROLE_USER_BUTTON: return "Front User Button";
    case DI_ROLE_BYPASS_SWITCH: return "Hardware Bypass Switch";
    case DI_ROLE_UNIT_0_ALARM_HP: return "Unit 0 HP Alarm";
    case DI_ROLE_UNIT_0_ALARM_LP: return "Unit 0 LP Alarm";
    case DI_ROLE_UNIT_0_ALARM_FLOW: return "Unit 0 Flow Alarm";
    case DI_ROLE_UNIT_0_ALARM_VOLT: return "Unit 0 Volt Alarm";
    case DI_ROLE_UNIT_1_ALARM_HP: return "Unit 1 HP Alarm";
    case DI_ROLE_UNIT_1_ALARM_LP: return "Unit 1 LP Alarm";
    case DI_ROLE_UNIT_1_ALARM_FLOW: return "Unit 1 Flow Alarm";
    case DI_ROLE_UNIT_1_ALARM_VOLT: return "Unit 1 Volt Alarm";
    case DI_ROLE_UNIT_2_ALARM_HP: return "Unit 2 HP Alarm";
    case DI_ROLE_UNIT_2_ALARM_LP: return "Unit 2 LP Alarm";
    case DI_ROLE_UNIT_2_ALARM_FLOW: return "Unit 2 Flow Alarm";
    case DI_ROLE_UNIT_2_ALARM_VOLT: return "Unit 2 Volt Alarm";
    case DI_ROLE_UNIT_3_ALARM_HP: return "Unit 3 HP Alarm";
    case DI_ROLE_UNIT_3_ALARM_LP: return "Unit 3 LP Alarm";
    case DI_ROLE_UNIT_3_ALARM_FLOW: return "Unit 3 Flow Alarm";
    case DI_ROLE_UNIT_3_ALARM_VOLT: return "Unit 3 Volt Alarm";
    case DI_ROLE_UNIT_4_ALARM_HP: return "Unit 4 HP Alarm";
    case DI_ROLE_UNIT_4_ALARM_LP: return "Unit 4 LP Alarm";
    case DI_ROLE_UNIT_4_ALARM_FLOW: return "Unit 4 Flow Alarm";
    case DI_ROLE_UNIT_4_ALARM_VOLT: return "Unit 4 Volt Alarm";
    case DI_ROLE_UNIT_5_ALARM_HP: return "Unit 5 HP Alarm";
    case DI_ROLE_UNIT_5_ALARM_LP: return "Unit 5 LP Alarm";
    case DI_ROLE_UNIT_5_ALARM_FLOW: return "Unit 5 Flow Alarm";
    case DI_ROLE_UNIT_5_ALARM_VOLT: return "Unit 5 Volt Alarm";
    case DI_ROLE_UNIT_6_ALARM_HP: return "Unit 6 HP Alarm";
    case DI_ROLE_UNIT_6_ALARM_LP: return "Unit 6 LP Alarm";
    case DI_ROLE_UNIT_6_ALARM_FLOW: return "Unit 6 Flow Alarm";
    case DI_ROLE_UNIT_6_ALARM_VOLT: return "Unit 6 Volt Alarm";
    case DI_ROLE_UNIT_7_ALARM_HP: return "Unit 7 HP Alarm";
    case DI_ROLE_UNIT_7_ALARM_LP: return "Unit 7 LP Alarm";
    case DI_ROLE_UNIT_7_ALARM_FLOW: return "Unit 7 Flow Alarm";
    case DI_ROLE_UNIT_7_ALARM_VOLT: return "Unit 7 Volt Alarm";
    default: return "Generic DI";
    }
}

/* =========================================================================
 * Lifecycle Management
 * ========================================================================= */

int app_facade_init(void *container)
{
    if (container == NULL) {
        return -EINVAL;
    }
    s_app = (app_container_t *)container;



    LOG_INF("Application Facade initialized");
    return 0;
}

/* =========================================================================
 * HVAC Control Service APIs
 * ========================================================================= */

int app_facade_hvac_get_status(app_facade_hvac_status_t *out_status)
{
    if (s_app == NULL || out_status == NULL) {
        return -EINVAL;
    }

    if (!s_app->hvac.initialized) {
        return -EAGAIN;
    }

    ao_hvac_t *ao = &s_app->hvac.ao;
    memset(out_status, 0, sizeof(*out_status));

    k_mutex_lock(&ao->lock, K_FOREVER);

    out_status->total_groups = s_app->hvac.config.total_groups;
    out_status->total_units = s_app->hvac.config.total_units;

    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        out_status->groups[g].group_id = g;
        out_status->groups[g].state = ao->groups[g].state;
        out_status->groups[g].bypass_causes_mask = ao->groups[g].bypass_causes_mask;
        out_status->groups[g].highest_temp = ao->group_metrics[g].highest_temp;
        out_status->groups[g].temp_min = ao->groups[g].temp_min;
        out_status->groups[g].temp_max = ao->groups[g].temp_max;
        out_status->groups[g].temp_alarm = ao->groups[g].temp_alarm;
        out_status->groups[g].temp_bypass = ao->groups[g].temp_bypass;
        out_status->groups[g].active_turn_count = ao->groups[g].active_turn_count;
        out_status->groups[g].current_turn_offset = ao->groups[g].current_turn_offset;
        out_status->groups[g].unit_mask = s_app->hvac.config.groups[g].unit_mask;
        out_status->groups[g].is_manual = ao->groups[g].is_manual_active;
        out_status->groups[g].sensor_start = s_app->hvac.config.groups[g].sensor_start;
        out_status->groups[g].sensor_end = s_app->hvac.config.groups[g].sensor_end;
        out_status->groups[g].metric_policy = s_app->hvac.config.groups[g].metric_policy;
        out_status->groups[g].rotation_interval_days = ao->groups[g].rotation_interval_days;
        out_status->groups[g].rotation_timer_s = ao->groups[g].rotation_timer_s;
        out_status->groups[g].enable_fan_delay = ao->groups[g].enable_fan_delay;
        out_status->groups[g].fan_delay_s = s_app->hvac.config.groups[g].fan_delay_s;
    }

    for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
        out_status->units[u].unit_id = u;
        out_status->units[u].state = ao->units[u].state;
        out_status->units[u].tri_state = hvac_unit_entity_get_tri_state(&ao->units[u]);
        hvac_unit_entity_get_relays(&ao->units[u],
                                    &out_status->units[u].relay_aa,
                                    &out_status->units[u].relay_sm,
                                    &out_status->units[u].relay_g);
        out_status->units[u].alarm_hp = ao->units[u].high_pressure_alarm;
        out_status->units[u].alarm_lp = ao->units[u].low_pressure_alarm;
        out_status->units[u].alarm_flow = ao->units[u].supply_fault_alarm;
        out_status->units[u].alarm_volt = ao->units[u].voltage_fault_alarm;
        out_status->units[u].fault_counter = ao->units[u].fault_counter;
    }

    k_mutex_unlock(&ao->lock);
    return 0;
}

int app_facade_hvac_set_mode(uint8_t group_id, hvac_mode_t mode)
{
    if (s_app == NULL || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    if (!s_app->hvac.initialized) {
        return -EAGAIN;
    }

    ao_hvac_t *ao = &s_app->hvac.ao;

    switch (mode) {
    case HVAC_MODE_OFF:
        (void)ao_hvac_set_group_mode_off(ao, group_id, true);
        break;

    case HVAC_MODE_COOL:
    case HVAC_MODE_AUTO:
        /* Restore normal automatic thermal evaluation */
        (void)ao_hvac_set_group_mode_off(ao, group_id, false);
        (void)ao_hvac_set_manual(ao, group_id, false);
        (void)ao_hvac_set_fire(ao, false);
        break;

    case HVAC_MODE_HEAT:
        /* For cooling-only HVAC units, auto cooling handles temperature */
        (void)ao_hvac_set_manual(ao, group_id, false);
        break;

    case HVAC_MODE_FAN_ONLY:
        /* Run fan only: manual override with compressor off (AA=0), fan enabled (G=0) */
        (void)ao_hvac_set_manual(ao, group_id, true);
        k_mutex_lock(&ao->lock, K_FOREVER);
        for (uint8_t u = 0U; u < ao->groups[group_id].unit_count; u++) {
            if (ao->groups[group_id].units[u] != NULL) {
                hvac_unit_entity_t *unit = ao->groups[group_id].units[u];
                hvac_unit_entity_set_manual(unit, false);
                unit->fan_state = HVAC_FAN_STATE_COMPRESSOR_RUNNING;
            }
        }
        k_mutex_unlock(&ao->lock);
        break;

    case HVAC_MODE_EMERGENCY:
        /* Emergency fire/safety shutdown */
        (void)ao_hvac_set_fire(ao, true);
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

int app_facade_hvac_set_group_mode(uint8_t group_id, hvac_mode_t mode)
{
    return app_facade_hvac_set_mode(group_id, mode);
}

int app_facade_hvac_set_setpoint(uint8_t group_id, int16_t cool_target_x10, int16_t heat_target_x10)
{
    if (s_app == NULL || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    if (!s_app->hvac.initialized) {
        return -EAGAIN;
    }

    if (heat_target_x10 > 0 && heat_target_x10 >= cool_target_x10) {
        return -EINVAL;
    }

    float cool_target = (float)cool_target_x10 / 10.0f;
    config_hvac_group_t group_cfg = s_app->hvac.config.groups[group_id];

    /* Preserve existing delta margins instead of hardcoding */
    float delta_alarm = group_cfg.temp_alarm - group_cfg.temp_max;
    if (delta_alarm <= 0.0f) {
        delta_alarm = 3.0f;
    }
    float delta_bypass = group_cfg.temp_bypass - group_cfg.temp_max;
    if (delta_bypass <= 0.0f) {
        delta_bypass = 6.0f;
    }

    group_cfg.temp_max = cool_target;
    if (heat_target_x10 > 0) {
        group_cfg.temp_min = (float)heat_target_x10 / 10.0f;
    } else {
        group_cfg.temp_min = cool_target - 2.0f;
    }

    group_cfg.temp_alarm = cool_target + delta_alarm;
    group_cfg.temp_bypass = cool_target + delta_bypass;

    return app_container_set_hvac_group_config(s_app, group_id, &group_cfg);
}

int app_facade_hvac_set_group_thermal_bounds(uint8_t group_id, float t_min, float t_max, float t_alarm, float t_bypass)
{
    if (s_app == NULL || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    if (!s_app->hvac.initialized) {
        return -EAGAIN;
    }

    config_hvac_group_t group_cfg = s_app->hvac.config.groups[group_id];
    if (t_min > -20.0f && t_min < 80.0f) {
        group_cfg.temp_min = t_min;
    }
    if (t_max > -20.0f && t_max < 80.0f) {
        group_cfg.temp_max = t_max;
    }
    if (t_alarm > -20.0f && t_alarm < 80.0f) {
        group_cfg.temp_alarm = t_alarm;
    }
    if (t_bypass > -20.0f && t_bypass < 80.0f) {
        group_cfg.temp_bypass = t_bypass;
    }

    return app_container_set_hvac_group_config(s_app, group_id, &group_cfg);
}

int app_facade_hvac_force_unit_rotation(uint8_t group_id)
{
    if (s_app == NULL || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    if (!s_app->hvac.initialized) {
        return -EAGAIN;
    }

    ao_hvac_t *ao = &s_app->hvac.ao;
    k_mutex_lock(&ao->lock, K_FOREVER);
    if (ao->group_active[group_id]) {
        hvac_group_entity_rotate_shift(&ao->groups[group_id]);
    }
    k_mutex_unlock(&ao->lock);

    return 0;
}

int app_facade_hvac_apply_profile(const hvac_profile_config_t *profile)
{
    if (s_app == NULL || profile == NULL) {
        return -EINVAL;
    }
    return app_container_apply_profile(s_app, profile->mode);
}

int app_facade_hvac_set_unit_manual(uint8_t unit_id, bool enable, bool run_compressor, bool run_fan)
{
    if (s_app == NULL || unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }
    if (!s_app->hvac.initialized) {
        return -EAGAIN;
    }

    return ao_hvac_set_unit_manual(&s_app->hvac.ao, unit_id, enable, run_compressor, run_fan);
}

int app_facade_hvac_configure_group_sensors(uint8_t group_id,
                                            uint8_t sensor_start,
                                            uint8_t sensor_end,
                                            sensor_metric_policy_t policy)
{
    if (s_app == NULL || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    if (!s_app->hvac.initialized) {
        return -EAGAIN;
    }
    if (!sensor_metric_policy_is_valid((uint8_t)policy)) {
        return -EINVAL;
    }
    if (sensor_start > 0U && sensor_start > sensor_end) {
        return -EINVAL;
    }

    s_app->hvac.config.groups[group_id].sensor_start = sensor_start;
    s_app->hvac.config.groups[group_id].sensor_end = sensor_end;
    s_app->hvac.config.groups[group_id].metric_policy = (uint8_t)policy;

    int rc = ao_hvac_set_group_config(&s_app->hvac.ao, group_id, &s_app->hvac.config.groups[group_id]);
    if (rc != 0) {
        return rc;
    }

    if (s_app->storage.initialized) {
        (void)ao_storage_mark_dirty(&s_app->storage.ao, CONFIG_DOMAIN_HVAC, (uint16_t)group_id);
    }

    return 0;
}

int app_facade_hvac_configure_group_units(uint8_t group_id,
                                          uint8_t active_turn_count,
                                          uint8_t unit_mask)
{
    if (s_app == NULL || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    if (!s_app->hvac.initialized) {
        return -EAGAIN;
    }
    if (active_turn_count > HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }

    s_app->hvac.config.groups[group_id].active_turn_count = active_turn_count;
    if (unit_mask != 0U) {
        s_app->hvac.config.groups[group_id].unit_mask = unit_mask;
    }

    int rc = ao_hvac_set_group_config(&s_app->hvac.ao, group_id, &s_app->hvac.config.groups[group_id]);
    if (rc != 0) {
        return rc;
    }

    if (s_app->storage.initialized) {
        (void)ao_storage_mark_dirty(&s_app->storage.ao, CONFIG_DOMAIN_HVAC, (uint16_t)group_id);
    }

    return 0;
}

int app_facade_hvac_set_group_rotation(uint8_t group_id, uint16_t days)
{
    if (s_app == NULL || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    if (!s_app->hvac.initialized) {
        return -EAGAIN;
    }
    if (days < 1U || days > 365U) {
        return -EINVAL;
    }

    config_hvac_group_t group_cfg = s_app->hvac.config.groups[group_id];
    group_cfg.rotation_interval_days = days;

    return app_container_set_hvac_group_config(s_app, group_id, &group_cfg);
}

int app_facade_hvac_set_group_fan_control(uint8_t group_id, bool enable, uint16_t fan_delay_s)
{
    if (s_app == NULL || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    if (!s_app->hvac.initialized) {
        return -EAGAIN;
    }

    config_hvac_group_t group_cfg = s_app->hvac.config.groups[group_id];
    group_cfg.enable_fan_delay = enable ? 1U : 0U;
    if (fan_delay_s > 0U) {
        group_cfg.fan_delay_s = fan_delay_s;
    }

    return app_container_set_hvac_group_config(s_app, group_id, &group_cfg);
}

int app_facade_hvac_set_test_temp(float temp)
{
    if (s_app == NULL || !s_app->hvac.initialized) {
        return -EINVAL;
    }
    return ao_hvac_set_test_temp(&s_app->hvac.ao, temp);
}

int app_facade_hvac_clear_test_temp(void)
{
    if (s_app == NULL || !s_app->hvac.initialized) {
        return -EINVAL;
    }
    return ao_hvac_clear_test_temp(&s_app->hvac.ao);
}

int app_facade_hvac_test_relay(uint8_t ch, bool state)
{
    if (s_app == NULL || !s_app->hvac.initialized) {
        return -EINVAL;
    }
    if (ch >= ADAPTER_RELAY_GPIO_CHANNEL_COUNT) {
        return -EINVAL;
    }
    const i_relay_t *port = &s_app->hvac.local_relay_port;
    if (!i_relay_is_bound(port)) {
        port = &s_app->hvac.actuator_adapter.local_relay_port;
    }
    if (!i_relay_is_bound(port)) {
        return -ENODEV;
    }
    return i_relay_set_state(port, (relay_channel_t)ch,
                             state ? RELAY_STATE_CLOSED : RELAY_STATE_OPEN);
}

int app_facade_hvac_reset_eeprom(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    /* 1. Pack k_default_hvac_config_hybrid_4units into CONFIG_DOMAIN_HVAC envelope */
    uint8_t hvac_buf[sizeof(config_envelope_header_t) + sizeof(config_hvac_t)];
    size_t pack_len = 0;
    int pack_rc = config_envelope_pack(CONFIG_DOMAIN_HVAC,
                                       &k_default_hvac_config_hybrid_4units,
                                       sizeof(k_default_hvac_config_hybrid_4units),
                                       hvac_buf, sizeof(hvac_buf), &pack_len);
    if (pack_rc != 0) {
        LOG_ERR("Failed to pack default HVAC envelope: %d", pack_rc);
        return pack_rc;
    }

    /* 2. Save directly to EEPROM storage */
    if (s_app->storage.initialized && i_config_storage_is_bound(&s_app->storage.port)) {
        int save_rc = i_config_storage_save(&s_app->storage.port, CONFIG_DOMAIN_HVAC,
                                            hvac_buf, pack_len);
        if (save_rc != 0) {
            LOG_ERR("Failed to save default HVAC config to EEPROM: %d", save_rc);
            return save_rc;
        }
        s_storage_stats.write_count++;
    }

    /* 3. Apply profile to container and active objects */
    return app_container_apply_profile(s_app, HVAC_MODE_HYBRID_FULL);
}

int app_facade_hvac_get_timers(uint8_t group_idx,
                               uint32_t *cooling_timer_s,
                               uint32_t *rotation_timer_s,
                               uint16_t *startup_timers,
                               uint16_t *fan_timers)
{
    if (s_app == NULL || !s_app->hvac.initialized) {
        return -EINVAL;
    }
    return ao_hvac_get_timers(&s_app->hvac.ao, group_idx,
                              cooling_timer_s, rotation_timer_s,
                              startup_timers, fan_timers);
}

/* =========================================================================
 * Network Service APIs
 * ========================================================================= */

#define APP_FACADE_CONTAINER_DEFINED 1
#include "app_facade_network.c"

/* =========================================================================
 * Sensor Matrix Service APIs
 * ========================================================================= */

int app_facade_sensor_get_readings(app_facade_sensor_view_t *out_view)
{
    if (out_view == NULL) {
        return -EINVAL;
    }

    if (s_app == NULL || !s_app->sensor.initialized) {
        return -ENODEV;
    }

    sensor_snapshot_msg_t snap;
    int err = zbus_chan_read(&chan_sensor_snapshot, &snap, K_NO_WAIT);
    if (err != 0) {
        return err;
    }

    raw_sensor_readings_t raw = {0};
    int raw_err = i_sensor_matrix_get_raw_readings(&s_app->sensor.port, &raw);

    out_view->internal_count = 5U;
    for (uint8_t i = 0U; i < 5U; i++) {
        int16_t offset = s_app->calibration_cfg.temp_offsets_x10[i];
        out_view->internal_rtd_temps[i] = snap.temperatures[i] + ((float)offset / 10.0f);
        out_view->internal_rtd_valid[i] = ((snap.valid_mask & (1ULL << i)) != 0ULL);

        if (raw_err == 0) {
            out_view->internal_rtd_raw_counts[i] = raw.internal_rtd_raw_adc[i];
            (void)transducer_rtd_adc_to_ohms(NULL, raw.internal_rtd_raw_adc[i], &out_view->internal_rtd_ohms[i]);
        } else {
            out_view->internal_rtd_raw_counts[i] = 0.0f;
            out_view->internal_rtd_ohms[i] = 0.0f;
        }
    }

    out_view->modbus_count = 16U;
    for (uint8_t i = 0U; i < 16U; i++) {
        int16_t offset = s_app->calibration_cfg.temp_offsets_x10[5U + i];
        uint8_t idx = 9U + i;
        out_view->modbus_rtd_temps[i] = snap.temperatures[idx] + ((float)offset / 10.0f);
        out_view->modbus_rtd_valid[i] = ((snap.valid_mask & (1ULL << idx)) != 0ULL);
    }

    return 0;
}

int app_facade_sensor_get_raw(raw_sensor_readings_t *out_raw)
{
    if (out_raw == NULL) {
        return -EINVAL;
    }

    if (s_app == NULL || !s_app->sensor.initialized) {
        return -ENODEV;
    }

    return i_sensor_matrix_get_raw_readings(&s_app->sensor.port, out_raw);
}

int app_facade_sensor_get_matrix(thermal_matrix_frame_t *out_frame)
{
    if (out_frame == NULL) {
        return -EINVAL;
    }

    if (s_app == NULL || !s_app->sensor.initialized) {
        return -ENODEV;
    }

    return zbus_chan_read(&chan_thermal_matrix, out_frame, K_NO_WAIT);
}

int app_facade_sensor_get_snapshot(sensor_snapshot_msg_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return -EINVAL;
    }

    if (s_app == NULL || !s_app->sensor.initialized) {
        return -ENODEV;
    }

    return zbus_chan_read(&chan_sensor_snapshot, out_snapshot, K_NO_WAIT);
}

int app_facade_sensor_set_calibration(uint8_t sensor_idx, int16_t offset_x10)
{
    if (sensor_idx >= 21U) {
        return -EINVAL;
    }
    s_sensor_cal_offsets[sensor_idx] = offset_x10;
    if (s_app != NULL) {
        s_app->calibration_cfg.temp_offsets_x10[sensor_idx] = offset_x10;
        if (s_app->storage.initialized) {
            (void)ao_storage_mark_dirty(&s_app->storage.ao, CONFIG_DOMAIN_CALIBRATION, (uint16_t)sensor_idx);
        }
    }
    return 0;
}

/* =========================================================================
 * Digital Input & Telemetry Service APIs
 * ========================================================================= */

int app_facade_di_get_status(app_facade_di_view_t *out_view)
{
    if (out_view == NULL) {
        return -EINVAL;
    }
    if (s_app == NULL || !s_app->di.initialized) {
        return -EAGAIN;
    }

    di_snapshot_msg_t snap;
    int err = zbus_chan_read(&chan_digital_input_snapshot, &snap, K_NO_WAIT);
    if (err != 0) {
        return err;
    }

    memset(out_view, 0, sizeof(*out_view));
    out_view->channel_count = snap.channel_count;
    out_view->active_mask = snap.active_mask;
    out_view->raw_mask = snap.raw_mask;

    for (size_t i = 0U; i < snap.channel_count && i < DI_CHANNEL_MAX; i++) {
        out_view->channels[i].index = (uint8_t)i;
        out_view->channels[i].role = snap.roles[i];
        out_view->channels[i].name = role_to_name(snap.roles[i]);
        out_view->channels[i].raw_level = ((snap.raw_mask & (1ULL << i)) != 0ULL);
        out_view->channels[i].is_active = snap.is_active[i];
        out_view->channels[i].active_duration_s = snap.active_duration_s[i];
    }

    return 0;
}

int app_facade_di_get_raw(uint64_t *out_raw_mask)
{
    if (out_raw_mask == NULL) {
        return -EINVAL;
    }
    if (s_app == NULL || !s_app->di.initialized) {
        return -EAGAIN;
    }

    di_snapshot_msg_t snap;
    int err = zbus_chan_read(&chan_digital_input_snapshot, &snap, K_NO_WAIT);
    if (err != 0) {
        return err;
    }

    *out_raw_mask = snap.raw_mask;
    return 0;
}

int app_facade_di_get_snapshot(di_snapshot_msg_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return -EINVAL;
    }
    if (s_app == NULL || !s_app->di.initialized) {
        return -EAGAIN;
    }

    return zbus_chan_read(&chan_digital_input_snapshot, out_snapshot, K_NO_WAIT);
}

const char *app_facade_di_role_name(di_role_t role)
{
    return role_to_name(role);
}

/* =========================================================================
 * Modbus Master Service APIs
 * ========================================================================= */

int app_facade_modbus_get_status(app_facade_modbus_view_t *out_view)
{
    if (s_app == NULL || out_view == NULL) {
        return -EINVAL;
    }

    memset(out_view, 0, sizeof(*out_view));

    for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++) {
        modbus_bus_cfg_t bcfg;
        if (modbus_registry_get_bus_cfg(&s_app->modbus.registry_impl, bus, &bcfg) == 0) {
            out_view->buses[bus].bus_id = bus;
            out_view->buses[bus].baudrate = bcfg.baudrate;
            out_view->buses[bus].parity = (uint8_t)bcfg.parity;
            out_view->buses[bus].stop_bits = bcfg.stop_bits;
            out_view->buses[bus].is_online = i_modbus_client_is_bound(&s_app->modbus.client_port[bus]);
        }

        modbus_slave_snapshot_t snaps[MODBUS_MAX_TOTAL_SLAVES];
        size_t count = 0U;
        if (modbus_registry_snapshot(&s_app->modbus.registry_impl, bus, snaps, MODBUS_MAX_TOTAL_SLAVES, &count) == 0) {
            out_view->buses[bus].slave_count = count;
            for (size_t i = 0U; i < count && out_view->total_slaves < MODBUS_MAX_TOTAL_SLAVES; i++) {
                size_t idx = out_view->total_slaves++;
                out_view->slaves[idx].device_id = snaps[i].descriptor.device_id;
                out_view->slaves[idx].bus_id = snaps[i].descriptor.bus_id;
                out_view->slaves[idx].slave_addr = snaps[i].descriptor.slave_addr;
                out_view->slaves[idx].dev_type = snaps[i].descriptor.dev_type;
                out_view->slaves[idx].poll_interval_ms = snaps[i].descriptor.poll_interval_ms;
                out_view->slaves[idx].timeout_ms = snaps[i].descriptor.timeout_ms;
                out_view->slaves[idx].is_enabled = snaps[i].descriptor.is_enabled;
                out_view->slaves[idx].health = snaps[i].runtime.health;
                out_view->slaves[idx].total_transactions = snaps[i].runtime.successful_polls +
                                                          snaps[i].runtime.timeout_count +
                                                          snaps[i].runtime.crc_error_count +
                                                          snaps[i].runtime.exception_count;
                out_view->slaves[idx].successful_transactions = snaps[i].runtime.successful_polls;
                out_view->slaves[idx].timeouts = snaps[i].runtime.timeout_count;
                out_view->slaves[idx].crc_errors = snaps[i].runtime.crc_error_count;
                out_view->slaves[idx].exceptions = snaps[i].runtime.exception_count;
            }
        }
    }

    return 0;
}

int app_facade_modbus_tcp_get_stats(modbus_tcp_server_stats_t *out_stats)
{
    if (s_app == NULL || out_stats == NULL) {
        return -EINVAL;
    }

    const i_modbus_tcp_server_t *server = app_container_get_modbus_tcp_server(s_app);
    if (server == NULL || !i_modbus_tcp_server_is_bound(server)) {
        return -ENODEV;
    }

    return i_modbus_tcp_server_get_stats(server, out_stats);
}

int app_facade_modbus_configure_slave(const modbus_slave_descriptor_t *slave)
{
    if (s_app == NULL || slave == NULL) {
        return -EINVAL;
    }

    int ret = modbus_registry_register(&s_app->modbus.registry_impl, slave);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

int app_facade_modbus_get_slave(uint8_t device_id, modbus_slave_descriptor_t *out_desc)
{
    if (s_app == NULL || out_desc == NULL) {
        return -EINVAL;
    }

    return modbus_registry_get(&s_app->modbus.registry_impl, device_id, out_desc);
}

int app_facade_modbus_configure_bus(uint8_t bus_id, const modbus_bus_config_t *cfg)
{
    if (s_app == NULL || bus_id >= (uint8_t)MODBUS_BUS_MAX || cfg == NULL) {
        return -EINVAL;
    }
    if (!modbus_bus_cfg_is_valid(cfg)) {
        return -EINVAL;
    }

    /* Apply configuration to physical UART transceiver */
    if (i_modbus_client_is_bound(&s_app->modbus.client_port[bus_id])) {
        int wire_err = i_modbus_client_set_bus_cfg(&s_app->modbus.client_port[bus_id], cfg);
        if (wire_err != 0) {
            LOG_ERR("bus %u: failed to set physical wire configuration: %d", bus_id, wire_err);
            return wire_err;
        }
    }

    /* Update container RAM cache */
    s_app->modbus.bus_cfg[bus_id] = *cfg;

    /* Update registry RAM state */
    int ret = modbus_registry_set_bus_cfg(&s_app->modbus.registry_impl, bus_id, cfg);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

/* =========================================================================
 * Storage / Persistence Service APIs
 * ========================================================================= */

int app_facade_storage_save_all(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }
    if (!s_app->storage.initialized) {
        return -EAGAIN;
    }

    int ret = ao_storage_sync_flush(&s_app->storage.ao);
    if (ret >= 0) {
        s_storage_stats.write_count += (uint32_t)ret;
        s_storage_stats.domains_persisted += (uint32_t)ret;
    } else {
        s_storage_stats.write_errors++;
    }
    return ret;
}

int app_facade_storage_reload_all(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }
    if (!s_app->storage.initialized || !i_config_storage_is_bound(&s_app->storage.port)) {
        return -EAGAIN;
    }
    s_storage_stats.read_count++;
    int reload_err = 0;

    /* 1. HVAC */
    if (s_app->hvac.initialized) {
        uint8_t buf[sizeof(config_envelope_header_t) + sizeof(config_hvac_t)];
        config_hvac_t hvac_cfg;
        memset(&hvac_cfg, 0, sizeof(hvac_cfg));
        size_t loaded_size = 0;
        size_t unpacked_size = 0;
        int ret = i_config_storage_load(&s_app->storage.port, CONFIG_DOMAIN_HVAC,
                                        buf, sizeof(buf), &loaded_size);
        if (ret == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_HVAC, buf, loaded_size,
                                   &hvac_cfg, sizeof(hvac_cfg), &unpacked_size) == 0 &&
            unpacked_size == sizeof(config_hvac_t) &&
            config_hvac_is_valid(&hvac_cfg)) {
            (void)app_container_set_hvac_config(s_app, &hvac_cfg);
        } else {
            reload_err = -EIO;
        }
    }

    /* 3. Ethernet */
    if (s_app->network.initialized) {
        net_eth_config_t eth_cfg;
        memset(&eth_cfg, 0, sizeof(eth_cfg));
        uint8_t eth_buf[sizeof(config_envelope_header_t) + sizeof(net_eth_config_t)];
        size_t loaded_size = 0;
        size_t unpacked_size = 0;
        int load_res = i_config_storage_load(&s_app->storage.port, CONFIG_DOMAIN_NET_ETH,
                                             eth_buf, sizeof(eth_buf), &loaded_size);
        if (load_res == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_NET_ETH, eth_buf, loaded_size,
                                   &eth_cfg, sizeof(eth_cfg), &unpacked_size) == 0 &&
            unpacked_size == sizeof(net_eth_config_t) &&
            net_eth_config_is_valid(&eth_cfg)) {
            if (i_network_is_bound(&s_app->network.eth_port)) {
                (void)i_network_set_config(&s_app->network.eth_port, &eth_cfg, sizeof(eth_cfg));
            }
        } else {
            reload_err = -EIO;
        }
    }

    /* 4. Wi-Fi */
    if (s_app->network.initialized) {
        net_wifi_config_t wifi_cfg;
        memset(&wifi_cfg, 0, sizeof(wifi_cfg));
        uint8_t wifi_buf[sizeof(config_envelope_header_t) + sizeof(net_wifi_config_t)];
        size_t loaded_size = 0;
        size_t unpacked_size = 0;
        int load_res = i_config_storage_load(&s_app->storage.port, CONFIG_DOMAIN_NET_WIFI,
                                             wifi_buf, sizeof(wifi_buf), &loaded_size);
        if (load_res == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_NET_WIFI, wifi_buf, loaded_size,
                                   &wifi_cfg, sizeof(wifi_cfg), &unpacked_size) == 0 &&
            unpacked_size == sizeof(net_wifi_config_t) &&
            net_wifi_config_is_valid(&wifi_cfg)) {
            if (i_network_is_bound(&s_app->network.wifi_port)) {
                (void)i_network_set_config(&s_app->network.wifi_port, &wifi_cfg, sizeof(wifi_cfg));
            }
        } else {
            reload_err = -EIO;
        }
    }

    /* 5. Device */
    {
        uint8_t buf[sizeof(config_envelope_header_t) + sizeof(config_device_t)];
        config_device_t dev_cfg;
        memset(&dev_cfg, 0, sizeof(dev_cfg));
        size_t loaded_size = 0;
        size_t unpacked_size = 0;
        int ret = i_config_storage_load(&s_app->storage.port, CONFIG_DOMAIN_DEVICE,
                                        buf, sizeof(buf), &loaded_size);
        if (ret == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_DEVICE, buf, loaded_size,
                                   &dev_cfg, sizeof(dev_cfg), &unpacked_size) == 0 &&
            unpacked_size == sizeof(config_device_t)) {
            s_app->device_cfg = dev_cfg;
        } else {
            reload_err = -EIO;
        }
    }

    /* 6. Calibration */
    {
        uint8_t buf[sizeof(config_envelope_header_t) + sizeof(config_calibration_t)];
        config_calibration_t cal_cfg;
        memset(&cal_cfg, 0, sizeof(cal_cfg));
        size_t loaded_size = 0;
        size_t unpacked_size = 0;
        int ret = i_config_storage_load(&s_app->storage.port, CONFIG_DOMAIN_CALIBRATION,
                                        buf, sizeof(buf), &loaded_size);
        if (ret == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_CALIBRATION, buf, loaded_size,
                                   &cal_cfg, sizeof(cal_cfg), &unpacked_size) == 0 &&
            unpacked_size == sizeof(config_calibration_t)) {
            s_app->calibration_cfg = cal_cfg;
        } else {
            reload_err = -EIO;
        }
    }

    /* 7. THERMAL_SITE */
    {
        uint8_t site_buf[sizeof(config_envelope_header_t) + sizeof(config_thermal_site_t)];
        config_thermal_site_t site_cfg;
        memset(&site_cfg, 0, sizeof(site_cfg));
        size_t loaded_site_size = 0;
        size_t unpacked_site_size = 0;
        int site_ret = i_config_storage_load(&s_app->storage.port, CONFIG_DOMAIN_THERMAL_SITE,
                                             site_buf, sizeof(site_buf), &loaded_site_size);
        if (site_ret == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_THERMAL_SITE, site_buf, loaded_site_size,
                                   &site_cfg, sizeof(site_cfg), &unpacked_site_size) == 0 &&
            unpacked_site_size == sizeof(config_thermal_site_t) &&
            config_thermal_site_is_valid(&site_cfg)) {
            s_app->thermal_site_cfg = site_cfg;
        } else {
            reload_err = -EIO;
        }
    }

    /* 8. THERMAL_BASELINE */
    {
        uint8_t base_buf[sizeof(config_envelope_header_t) + sizeof(config_thermal_baseline_t)];
        config_thermal_baseline_t base_cfg;
        memset(&base_cfg, 0, sizeof(base_cfg));
        size_t loaded_base_size = 0;
        size_t unpacked_base_size = 0;
        int base_ret = i_config_storage_load(&s_app->storage.port, CONFIG_DOMAIN_THERMAL_BASELINE,
                                             base_buf, sizeof(base_buf), &loaded_base_size);
        if (base_ret == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_THERMAL_BASELINE, base_buf, loaded_base_size,
                                   &base_cfg, sizeof(base_cfg), &unpacked_base_size) == 0 &&
            unpacked_base_size == sizeof(config_thermal_baseline_t) &&
            config_thermal_baseline_is_valid(&base_cfg)) {
            s_app->thermal_baseline_cfg = base_cfg;
        } else {
            reload_err = -EIO;
        }
    }



    return reload_err;
}

int app_facade_storage_reset_defaults(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    /* 1. Reset storage slot states in medium */
    if (s_app->storage.initialized && i_config_storage_is_bound(&s_app->storage.port)) {
        for (int d = 0; d < CONFIG_DOMAIN_MAX; d++) {
            (void)i_config_storage_reset_defaults(&s_app->storage.port, (config_domain_t)d);
        }
        s_storage_stats.write_count++;
    }

    /* 2. Reset RAM domains to factory defaults */
    s_app->device_cfg = *default_config_get_device_cfg();
    s_app->calibration_cfg = *default_config_get_calibration_cfg();

    /* 3. Reset Modbus bus configs & registry */
    if (s_app->modbus.initialized) {
        for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++) {
            const modbus_bus_cfg_t *bcfg = modbus_device_table_get_bus_cfg((modbus_bus_id_t)bus);
            if (bcfg != NULL) {
                s_app->modbus.bus_cfg[bus] = *bcfg;
            }
            if (s_app->modbus.client_port[bus].api != NULL) {
                (void)i_modbus_client_set_bus_cfg(&s_app->modbus.client_port[bus], &s_app->modbus.bus_cfg[bus]);
            }
        }
        (void)modbus_registry_init(&s_app->modbus.registry_impl, s_app->modbus.bus_cfg);
        size_t def_count = 0;
        const modbus_slave_descriptor_t *def_slaves = modbus_device_table_get_slaves(&def_count);
        for (size_t i = 0; i < def_count; i++) {
            (void)modbus_registry_register(&s_app->modbus.registry_impl, &def_slaves[i]);
        }
    }

    /* 4. Reset Network (Ethernet & Wi-Fi) */
    if (s_app->network.initialized) {
        const net_eth_config_t *eth_def = default_config_get_net_eth_cfg();
        const net_wifi_config_t *wifi_def = default_config_get_net_wifi_cfg();
        if (eth_def != NULL && i_network_is_bound(&s_app->network.eth_port)) {
            (void)i_network_set_config(&s_app->network.eth_port, eth_def, sizeof(*eth_def));
        }
        if (wifi_def != NULL && i_network_is_bound(&s_app->network.wifi_port)) {
            (void)i_network_set_config(&s_app->network.wifi_port, wifi_def, sizeof(*wifi_def));
        }
    }

    /* 5. Re-apply baseline default profile to RAM & hardware actuators/DIs */
    return app_container_apply_profile(s_app, HVAC_MODE_HYBRID_FULL);
}

int app_facade_storage_get_stats(storage_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return -EINVAL;
    }
    *out_stats = s_storage_stats;
    return 0;
}

/* =========================================================================
 * System / Diagnostics Service APIs
 * ========================================================================= */

int app_facade_system_get_info(app_facade_system_info_t *out_info)
{
    if (s_app == NULL || out_info == NULL) {
        return -EINVAL;
    }

    out_info->firmware_version = "v3.0.0";
    out_info->uptime_seconds = (uint64_t)(k_uptime_get() / 1000LL);

    hvac_operation_mode_t op_mode = (hvac_operation_mode_t)s_app->hvac.config.operation_mode;
    switch (op_mode) {
    case HVAC_MODE_TELEMETRY_ONLY:
        out_info->profile_name = "Telemetry-Only (Monitoring)";
        break;
    case HVAC_MODE_HVAC_CONTROL_ONLY:
        out_info->profile_name = "Control-Only (8-Unit Staged)";
        break;
    case HVAC_MODE_HYBRID_FULL:
        out_info->profile_name = "Hybrid Full (4-Unit Psychrometric)";
        break;
    default:
        out_info->profile_name = "Custom / Unknown";
        break;
    }

    out_info->free_heap_bytes = 0U;
    out_info->total_heap_bytes = CONFIG_HEAP_MEM_POOL_SIZE;

    return 0;
}

int app_facade_system_reboot(void)
{
    LOG_WRN("Reboot requested through Application Facade");
    k_sleep(K_MSEC(100));
    sys_reboot(SYS_REBOOT_COLD);
    return 0;
}

/* =========================================================================
 * Alarm Management Service APIs
 * ========================================================================= */

int app_facade_alarm_get_status(app_facade_alarm_status_t *out_status)
{
    if (s_app == NULL || out_status == NULL) {
        return -EINVAL;
    }

    const i_alarm_query_t *query = app_container_get_alarm_query(s_app);
    if (query == NULL || !i_alarm_query_is_bound(query)) {
        return -ENODEV;
    }

    ao_alarm_manager_t *ao = app_container_get_alarm_manager_ao(s_app);

    memset(out_status, 0, sizeof(*out_status));
    out_status->active_count = i_alarm_query_get_active_count(query);

    alarm_severity_t sev = i_alarm_query_get_highest_active_severity(query);
    if (out_status->active_count == 0U) {
        out_status->highest_level = ALARM_LEVEL_NONE;
    } else if (sev == ALARM_SEVERITY_FATAL || sev == ALARM_SEVERITY_CRITICAL) {
        out_status->highest_level = ALARM_LEVEL_CRITICAL;
    } else if (sev == ALARM_SEVERITY_WARNING) {
        out_status->highest_level = ALARM_LEVEL_WARNING;
    } else {
        out_status->highest_level = ALARM_LEVEL_INFO;
    }

    if (ao != NULL) {
        out_status->total_registered = alarm_registry_get_count(&ao->registry);
    }

    /* Count acknowledged alarms among active ones */
    uint16_t ids[CONFIG_ALARM_MANAGER_MAX_ALARMS];
    size_t count = i_alarm_query_get_active_alarm_ids(query, ids, ARRAY_SIZE(ids));
    size_t acked = 0U;
    for (size_t i = 0U; i < count; i++) {
        alarm_state_t state = ALARM_STATE_INACTIVE;
        if (i_alarm_query_get_alarm_state(query, ids[i], &state) == 0 &&
            state == ALARM_STATE_ACKNOWLEDGED) {
            acked++;
        }
    }
    out_status->acknowledged_count = acked;
    out_status->buzzer_muted = app_container_is_alarm_buzzer_muted(s_app);

    return 0;
}

size_t app_facade_alarm_get_active_list(app_facade_alarm_item_t *out_items, size_t max_items)
{
    if (s_app == NULL || out_items == NULL || max_items == 0U) {
        return 0U;
    }

    const i_alarm_query_t *query = app_container_get_alarm_query(s_app);
    if (query == NULL || !i_alarm_query_is_bound(query)) {
        return 0U;
    }

    uint16_t ids[CONFIG_ALARM_MANAGER_MAX_ALARMS];
    size_t count = i_alarm_query_get_active_alarm_ids(query, ids, ARRAY_SIZE(ids));
    if (count > max_items) {
        count = max_items;
    }

    for (size_t i = 0U; i < count; i++) {
        uint16_t id = ids[i];
        alarm_snapshot_entry_t snap = {0};
        alarm_descriptor_t desc = {0};

        (void)i_alarm_query_get_alarm_snapshot(query, id, &snap);
        (void)i_alarm_query_get_descriptor(query, id, &desc);

        out_items[i].alarm_id = id;
        out_items[i].state = snap.state;
        out_items[i].category = desc.source_category;
        out_items[i].name = desc.name ? desc.name : "UNKNOWN";
        out_items[i].trigger_val = snap.last_trigger_value;
        out_items[i].active_duration_ms = i_alarm_query_get_active_duration(query, id);
        out_items[i].acknowledged = (snap.state == ALARM_STATE_ACKNOWLEDGED);

        if (snap.severity == ALARM_SEVERITY_FATAL || snap.severity == ALARM_SEVERITY_CRITICAL) {
            out_items[i].level = ALARM_LEVEL_CRITICAL;
        } else if (snap.severity == ALARM_SEVERITY_WARNING) {
            out_items[i].level = ALARM_LEVEL_WARNING;
        } else {
            out_items[i].level = ALARM_LEVEL_INFO;
        }
    }

    return count;
}

size_t app_facade_alarm_get_catalog(const alarm_descriptor_t **out_catalog, size_t max_items)
{
    if (s_app == NULL || out_catalog == NULL || max_items == 0U) {
        return 0U;
    }

    ao_alarm_manager_t *ao = app_container_get_alarm_manager_ao(s_app);
    if (ao == NULL) {
        return 0U;
    }

    size_t total = alarm_registry_get_count(&ao->registry);
    size_t to_copy = (total < max_items) ? total : max_items;

    for (size_t i = 0U; i < to_copy; i++) {
        out_catalog[i] = &ao->registry.descriptors[i];
    }

    return to_copy;
}

int app_facade_alarm_get_snapshot(uint16_t alarm_id,
                                  alarm_snapshot_entry_t *out_snapshot,
                                  const alarm_descriptor_t **out_desc)
{
    if (s_app == NULL || out_snapshot == NULL) {
        return -EINVAL;
    }

    const i_alarm_query_t *query = app_container_get_alarm_query(s_app);
    if (query == NULL || !i_alarm_query_is_bound(query)) {
        return -ENODEV;
    }

    int ret = i_alarm_query_get_alarm_snapshot(query, alarm_id, out_snapshot);
    if (ret != 0) {
        return ret;
    }

    if (out_desc != NULL) {
        ao_alarm_manager_t *ao = app_container_get_alarm_manager_ao(s_app);
        if (ao != NULL) {
            int idx = alarm_registry_find_index(&ao->registry, alarm_id);
            if (idx >= 0) {
                *out_desc = &ao->registry.descriptors[idx];
            } else {
                *out_desc = NULL;
            }
        } else {
            *out_desc = NULL;
        }
    }

    return 0;
}

int app_facade_alarm_ack(uint16_t alarm_id)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    ao_alarm_manager_t *ao = app_container_get_alarm_manager_ao(s_app);
    if (ao == NULL) {
        return -ENODEV;
    }

    return ao_alarm_manager_post_ack(ao, alarm_id);
}

int app_facade_alarm_reset_latch(uint16_t alarm_id)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    ao_alarm_manager_t *ao = app_container_get_alarm_manager_ao(s_app);
    if (ao == NULL) {
        return -ENODEV;
    }

    return ao_alarm_manager_post_reset_latch(ao, alarm_id);
}

int app_facade_alarm_set_buzzer_mute(bool muted)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    return app_container_set_alarm_buzzer_mute(s_app, muted);
}

bool app_facade_alarm_is_buzzer_muted(void)
{
    if (s_app == NULL) {
        return false;
    }

    return app_container_is_alarm_buzzer_muted(s_app);
}

/* =========================================================================
 * Thermal and Energy Subsystem Presentation APIs
 * ========================================================================= */

int app_facade_thermal_get_result(thermal_energy_result_t *out_result)
{
    if (s_app == NULL || out_result == NULL) {
        return -EINVAL;
    }

    i_thermal_energy_t port = composition_root_get_thermal_energy_port(s_app);
    return i_thermal_energy_get_latest_result(&port, out_result);
}

int app_facade_thermal_get_capture(thermal_energy_capture_t *out_capture)
{
    if (s_app == NULL || out_capture == NULL) {
        return -EINVAL;
    }

    i_thermal_energy_t port = composition_root_get_thermal_energy_port(s_app);
    return i_thermal_energy_get_latest_capture(&port, out_capture);
}

int app_facade_thermal_get_hourly_history(uint16_t hours_ago, thermal_hourly_record_t *out_record)
{
    if (s_app == NULL || out_record == NULL) {
        return -EINVAL;
    }

    i_thermal_energy_t port = composition_root_get_thermal_energy_port(s_app);
    return i_thermal_energy_get_hourly_history(&port, hours_ago, out_record);
}

int app_facade_thermal_get_baseline(config_thermal_baseline_t *out_baseline)
{
    if (s_app == NULL || out_baseline == NULL) {
        return -EINVAL;
    }

    i_thermal_energy_t port = composition_root_get_thermal_energy_port(s_app);
    return i_thermal_energy_get_baseline(&port, out_baseline);
}

int app_facade_thermal_get_site_config(config_thermal_site_t *out_site_cfg)
{
    if (s_app == NULL || out_site_cfg == NULL) {
        return -EINVAL;
    }

    const config_thermal_site_t *cfg = app_container_get_thermal_site_config(s_app);
    if (cfg == NULL) {
        return -ENODATA;
    }

    *out_site_cfg = *cfg;
    return 0;
}

int app_facade_thermal_trigger_recalibration(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    i_thermal_energy_t port = composition_root_get_thermal_energy_port(s_app);
    return i_thermal_energy_trigger_baseline_recalibration(&port);
}

/* =========================================================================
 * Real-Time Clock & System Time Service APIs
 * ========================================================================= */

int app_facade_time_get_datetime(rtc_date_time_t *out_dt)
{
    if (s_app == NULL || out_dt == NULL) {
        return -EINVAL;
    }

    const i_rtc_t *rtc = app_container_get_rtc(s_app);
    if (rtc == NULL || !i_rtc_is_bound(rtc)) {
        return -ENODEV;
    }

    return i_rtc_get_time(rtc, out_dt);
}

int app_facade_time_set_datetime(const rtc_date_time_t *dt, rtc_time_source_t source)
{
    if (s_app == NULL || dt == NULL) {
        return -EINVAL;
    }

    if (!rtc_calc_is_valid_datetime(dt)) {
        return -EINVAL;
    }

    const i_rtc_t *rtc = app_container_get_rtc(s_app);
    if (rtc == NULL || !i_rtc_is_bound(rtc)) {
        return -ENODEV;
    }

    int err = i_rtc_set_time(rtc, dt);
    if (err != 0) {
        return err;
    }

    rtc_sync_status_t status;
    memset(&status, 0, sizeof(status));
    (void)i_rtc_get_sync_status(rtc, &status);

    if (source == RTC_TIME_SOURCE_SNTP || source == RTC_TIME_SOURCE_CLOUD_MQTT) {
        status.sync_state = RTC_SYNC_STATE_SYNCHRONIZED;
    } else {
        status.sync_state = RTC_SYNC_STATE_BATTERY_RETAINED;
    }
    status.last_sync_source = source;
    (void)rtc_calc_datetime_to_epoch(dt, &status.last_sync_epoch_s);
    status.last_sync_uptime_ms = k_uptime_get();
    status.sync_count++;
    status.battery_ok = true;

    (void)i_rtc_set_sync_status(rtc, &status);
    return 0;
}

int app_facade_time_get_unix(int64_t *out_epoch_s, uint16_t *out_subsecond_ms)
{
    if (s_app == NULL || out_epoch_s == NULL) {
        return -EINVAL;
    }

    const i_rtc_t *rtc = app_container_get_rtc(s_app);
    if (rtc == NULL || !i_rtc_is_bound(rtc)) {
        return -ENODEV;
    }

    uint32_t ms = 0;
    int err = i_rtc_get_epoch(rtc, out_epoch_s, &ms);
    if (err != 0) {
        return err;
    }

    if (out_subsecond_ms != NULL) {
        *out_subsecond_ms = (uint16_t)ms;
    }
    return 0;
}

int app_facade_time_get_epoch(int64_t *out_sec, uint32_t *out_ms)
{
    if (s_app == NULL || out_sec == NULL) {
        return -EINVAL;
    }

    const i_rtc_t *rtc = app_container_get_rtc(s_app);
    if (rtc == NULL || !i_rtc_is_bound(rtc)) {
        return -ENODEV;
    }

    return i_rtc_get_epoch(rtc, out_sec, out_ms);
}

int app_facade_time_set_unix(int64_t epoch_s, rtc_time_source_t source)
{
    if (s_app == NULL || epoch_s < 0) {
        return -EINVAL;
    }

    const i_rtc_t *rtc = app_container_get_rtc(s_app);
    if (rtc == NULL || !i_rtc_is_bound(rtc)) {
        return -ENODEV;
    }

    int err = i_rtc_set_epoch(rtc, epoch_s, 0);
    if (err != 0) {
        return err;
    }

    rtc_sync_status_t status;
    memset(&status, 0, sizeof(status));
    (void)i_rtc_get_sync_status(rtc, &status);

    if (source == RTC_TIME_SOURCE_SNTP || source == RTC_TIME_SOURCE_CLOUD_MQTT) {
        status.sync_state = RTC_SYNC_STATE_SYNCHRONIZED;
    } else {
        status.sync_state = RTC_SYNC_STATE_BATTERY_RETAINED;
    }
    status.last_sync_source = source;
    status.last_sync_epoch_s = epoch_s;
    status.last_sync_uptime_ms = k_uptime_get();
    status.sync_count++;
    status.battery_ok = true;

    (void)i_rtc_set_sync_status(rtc, &status);
    return 0;
}

int app_facade_time_get_iso8601(char *buf, size_t buf_sz)
{
    if (buf == NULL || buf_sz < 25) {
        return -EINVAL;
    }

    rtc_date_time_t dt;
    int err = app_facade_time_get_datetime(&dt);
    if (err != 0) {
        return err;
    }

    return rtc_calc_format_iso8601(&dt, buf, buf_sz);
}

int app_facade_time_format_iso8601(char *buf, size_t buf_sz)
{
    return app_facade_time_get_iso8601(buf, buf_sz);
}

int app_facade_time_get_status(rtc_sync_status_t *out_status)
{
    if (s_app == NULL || out_status == NULL) {
        return -EINVAL;
    }

    const i_rtc_t *rtc = app_container_get_rtc(s_app);
    if (rtc == NULL || !i_rtc_is_bound(rtc)) {
        return -ENODEV;
    }

    return i_rtc_get_sync_status(rtc, out_status);
}

uint32_t app_facade_time_get_fattime(void)
{
    rtc_date_time_t dt;
    int err = app_facade_time_get_datetime(&dt);
    if (err != 0) {
        return rtc_calc_datetime_to_fattime(NULL);
    }

    return rtc_calc_datetime_to_fattime(&dt);
}

/* =========================================================================
 * MicroSD Storage & Filesystem Service APIs
 * ========================================================================= */

int app_facade_sd_get_status(storage_fs_card_status_t *out_status, storage_fs_volume_info_t *out_vol)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    ao_storage_sd_t *ao = app_container_get_sd_storage_ao(s_app);
    if (ao == NULL) {
        return -ENODEV;
    }

    return ao_storage_sd_get_status(ao, out_status, out_vol);
}

int app_facade_sd_mount(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    ao_storage_sd_t *ao = app_container_get_sd_storage_ao(s_app);
    if (ao == NULL) {
        return -ENODEV;
    }

    return ao_storage_sd_mount(ao);
}

int app_facade_sd_eject(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    ao_storage_sd_t *ao = app_container_get_sd_storage_ao(s_app);
    if (ao == NULL) {
        return -ENODEV;
    }

    return ao_storage_sd_eject(ao);
}

const i_storage_fs_t *app_facade_get_sd_storage(void)
{
    if (s_app == NULL) {
        return NULL;
    }

    return app_container_get_sd_storage(s_app);
}


