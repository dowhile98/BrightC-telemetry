/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file app_facade.c
 * @brief Implementation of Application Service Facade.
 */

#include "application/services/app_facade.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "infrastructure/di/composition_root.h"
#include "common/config_envelope.h"
#include "common/app_defaults.h"
#include "common/modbus_device_table.h"

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
    case DI_ROLE_COMPRESSOR_1_AUX: return "Comp 1 Run Aux";
    case DI_ROLE_COMPRESSOR_2_AUX: return "Comp 2 Run Aux";
    case DI_ROLE_COMPRESSOR_3_AUX: return "Comp 3 Run Aux";
    case DI_ROLE_COMPRESSOR_4_AUX: return "Comp 4 Run Aux";
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
        /* Force manual idle */
        (void)ao_hvac_set_manual(ao, group_id, true);
        k_mutex_lock(&ao->lock, K_FOREVER);
        for (uint8_t u = 0U; u < ao->groups[group_id].unit_count; u++) {
            if (ao->groups[group_id].units[u] != NULL) {
                hvac_unit_entity_set_manual(ao->groups[group_id].units[u], false);
            }
        }
        k_mutex_unlock(&ao->lock);
        break;

    case HVAC_MODE_COOL:
    case HVAC_MODE_AUTO:
        /* Restore normal automatic thermal evaluation */
        (void)ao_hvac_set_manual(ao, group_id, false);
        (void)ao_hvac_set_fire(ao, false);
        break;

    case HVAC_MODE_HEAT:
        /* For cooling-only HVAC units, auto cooling handles temperature */
        (void)ao_hvac_set_manual(ao, group_id, false);
        break;

    case HVAC_MODE_FAN_ONLY:
        /* Run fan purge: manual override with compressor off, fan purge enabled */
        (void)ao_hvac_set_manual(ao, group_id, true);
        k_mutex_lock(&ao->lock, K_FOREVER);
        for (uint8_t u = 0U; u < ao->groups[group_id].unit_count; u++) {
            if (ao->groups[group_id].units[u] != NULL) {
                hvac_unit_entity_t *unit = ao->groups[group_id].units[u];
                hvac_unit_entity_set_manual(unit, false);
                unit->fan_state = HVAC_FAN_STATE_SMART_IDLE_ON;
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

    ao_hvac_t *ao = &s_app->hvac.ao;
    k_mutex_lock(&ao->lock, K_FOREVER);
    if (enable) {
        hvac_unit_entity_set_manual(&ao->units[unit_id], run_compressor);
        if (!run_compressor && run_fan) {
            ao->units[unit_id].fan_state = HVAC_FAN_STATE_SMART_IDLE_ON;
        } else if (!run_compressor && !run_fan) {
            ao->units[unit_id].fan_state = HVAC_FAN_STATE_DISABLED;
        }
    } else {
        /* Restore to OFF/auto */
        hvac_unit_entity_request_stop(&ao->units[unit_id], false);
    }
    k_mutex_unlock(&ao->lock);

    return 0;
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

    out_view->internal_count = 5U;
    for (uint8_t i = 0U; i < 5U; i++) {
        int16_t offset = s_app->calibration_cfg.temp_offsets_x10[i];
        out_view->internal_rtd_temps[i] = snap.temperatures[i] + ((float)offset / 10.0f);
        out_view->internal_rtd_valid[i] = ((snap.valid_mask & (1ULL << i)) != 0ULL);
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
    if (s_app == NULL || out_view == NULL) {
        return -EINVAL;
    }

    di_aggregator_t *agg = &s_app->di.aggregator;
    memset(out_view, 0, sizeof(*out_view));

    out_view->channel_count = (uint8_t)agg->channel_count;

    for (size_t i = 0U; i < agg->channel_count && i < DI_CHANNEL_MAX; i++) {
        out_view->channels[i].index = (uint8_t)i;
        out_view->channels[i].role = agg->configs[i].role;
        out_view->channels[i].name = role_to_name(agg->configs[i].role);
        out_view->channels[i].raw_level = agg->statuses[i].raw_level;
        out_view->channels[i].is_active = agg->statuses[i].is_active;
        out_view->channels[i].active_duration_s = agg->statuses[i].active_duration_s;
    }

    (void)di_aggregator_get_active_mask(agg, &out_view->active_mask);
    (void)app_facade_di_get_raw(&out_view->raw_mask);

    return 0;
}

int app_facade_di_get_raw(uint64_t *out_raw_mask)
{
    if (s_app == NULL || out_raw_mask == NULL) {
        return -EINVAL;
    }

    di_aggregator_t *agg = &s_app->di.aggregator;
    uint64_t mask = 0ULL;

    for (size_t i = 0U; i < agg->channel_count && i < 64U; i++) {
        if (agg->raw_inputs[i]) {
            mask |= (1ULL << i);
        }
    }

    *out_raw_mask = mask;
    return 0;
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
