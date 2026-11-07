/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file shell_brightc.c
 * @brief Zephyr Shell presentation interface for BrightC Telemetry.
 *
 * Adheres strictly to Clean Architecture: Presentation layer interacts ONLY
 * with the Application Service Facade (app_facade.h). Never includes container
 * internals, Active Objects, or raw hardware driver headers.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "application/services/app_facade.h"

/* =========================================================================
 * Helper String Formatters
 * ========================================================================= */

static const char *hvac_group_state_str(hvac_group_state_t state)
{
    switch (state) {
    case GROUP_STATE_IDLE:       return "IDLE";
    case GROUP_STATE_COOLING:    return "COOLING";
    case GROUP_STATE_COOL_BOOST: return "COOL_BOOST";
    case GROUP_STATE_BYPASS:     return "BYPASS";
    case GROUP_STATE_FIRE:       return "FIRE_SHUTDOWN";
    default:                     return "UNKNOWN";
    }
}

static const char *hvac_metric_policy_str(uint8_t policy)
{
    switch (policy) {
    case SENSOR_METRIC_AVG: return "AVG";
    case SENSOR_METRIC_MIN: return "MIN";
    case SENSOR_METRIC_MAX:
    default:
        return "MAX";
    }
}

static const char *hvac_unit_state_str(hvac_unit_state_t state)
{
    switch (state) {
    case UNIT_STATE_OFF:           return "OFF";
    case UNIT_STATE_STARTING:      return "STARTING";
    case UNIT_STATE_RUNNING:       return "RUNNING";
    case UNIT_STATE_STOPPING:      return "STOPPING";
    case UNIT_STATE_BLOCKED_ALARM: return "BLOCKED_ALARM";
    case UNIT_STATE_FAULT:         return "FAULT";
    case UNIT_STATE_BYPASS:        return "BYPASS";
    default:                       return "UNKNOWN";
    }
}

static const char *modbus_health_str(modbus_slave_health_t health)
{
    switch (health) {
    case MODBUS_HEALTH_ONLINE:          return "ONLINE";
    case MODBUS_HEALTH_DEGRADED:        return "DEGRADED";
    case MODBUS_HEALTH_OFFLINE_BACKOFF: return "OFFLINE";
    default:                            return "UNKNOWN";
    }
}

static const char *modbus_parity_str(uint8_t parity)
{
    switch (parity) {
    case MODBUS_PARITY_NONE: return "None";
    case MODBUS_PARITY_ODD:  return "Odd";
    case MODBUS_PARITY_EVEN: return "Even";
    default:                 return "Unknown";
    }
}

/* =========================================================================
 * HVAC Shell Commands
 * ========================================================================= */

static int cmd_hvac_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    app_facade_hvac_status_t status;
    int err = app_facade_hvac_get_status(&status);
    if (err != 0) {
        shell_error(sh, "Failed to get HVAC status (%d)", err);
        return err;
    }

    shell_print(sh, "=== Industrial HVAC Subsystem Status ===");
    shell_print(sh, "Total Groups: %u | Total Units: %u\n", status.total_groups, status.total_units);

    for (uint8_t g = 0U; g < status.total_groups; g++) {
        shell_print(sh, "--- Group %u ---", g);
        shell_print(sh, "  State:        %s", hvac_group_state_str(status.groups[g].state));
        shell_print(sh, "  Mode:         %s", status.groups[g].is_manual ? "MANUAL" : "AUTO");
        shell_print(sh, "  Sensors:      IDs %u..%u (Policy: %s)",
                    status.groups[g].sensor_start,
                    status.groups[g].sensor_end,
                    hvac_metric_policy_str(status.groups[g].metric_policy));
        shell_print(sh, "  Temp Highest: %.1f C", (double)status.groups[g].highest_temp);
        shell_print(sh, "  Setpoints:    Min=%.1f C, Max=%.1f C, Boost=%.1f C, Bypass=%.1f C",
                    (double)status.groups[g].temp_min, (double)status.groups[g].temp_max,
                    (double)status.groups[g].temp_alarm, (double)status.groups[g].temp_bypass);
        shell_print(sh, "  Turn Count:   %u active | Offset: %u (mask=0x%02X)",
                    status.groups[g].active_turn_count, status.groups[g].current_turn_offset,
                    status.groups[g].unit_mask);
        uint32_t rem_s = status.groups[g].rotation_timer_s;
        uint32_t days = rem_s / 86400U;
        rem_s %= 86400U;
        uint32_t hours = rem_s / 3600U;
        rem_s %= 3600U;
        uint32_t mins = rem_s / 60U;
        shell_print(sh, "  Rotation:     %ud (Elapsed: %ud %uh %um)",
                    status.groups[g].rotation_interval_days,
                    days, hours, mins);
        if (status.groups[g].bypass_causes_mask != 0) {
            shell_warn(sh, "  Bypass Mask:  0x%08X", status.groups[g].bypass_causes_mask);
        }
    }

    shell_print(sh, "\n--- AC Units Detail ---");
    shell_print(sh, "ID | State         | Tri-State | Relays (AA,SM,G) | Alarms (HP,LP,Flow,Volt) | Faults");
    shell_print(sh, "----------------------------------------------------------------------------------");
    for (uint8_t u = 0U; u < status.total_units; u++) {
        shell_print(sh, " %u | %-13s | %-9s | %c  %c  %c          | %c  %c  %c    %c             | %u",
                    u,
                    hvac_unit_state_str(status.units[u].state),
                    (status.units[u].tri_state == HVAC_TRI_STATE_ON) ? "ON" :
                    (status.units[u].tri_state == HVAC_TRI_STATE_BYPASS) ? "BYPASS" : "OFF",
                    status.units[u].relay_aa ? '1' : '0',
                    status.units[u].relay_sm ? '1' : '0',
                    status.units[u].relay_g ? '1' : '0',
                    status.units[u].alarm_hp ? 'X' : '.',
                    status.units[u].alarm_lp ? 'X' : '.',
                    status.units[u].alarm_flow ? 'X' : '.',
                    status.units[u].alarm_volt ? 'X' : '.',
                    status.units[u].fault_counter);
    }

    return 0;
}

static int cmd_hvac_mode(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: hvac mode <grp> <off|cool|heat|auto|fan_only|emergency>");
        return -EINVAL;
    }

    uint8_t group_id = (uint8_t)strtoul(argv[1], NULL, 10);
    const char *mode_str = argv[2];
    hvac_mode_t mode;

    if (strcmp(mode_str, "off") == 0) {
        mode = HVAC_MODE_OFF;
    } else if (strcmp(mode_str, "cool") == 0) {
        mode = HVAC_MODE_COOL;
    } else if (strcmp(mode_str, "heat") == 0) {
        mode = HVAC_MODE_HEAT;
    } else if (strcmp(mode_str, "auto") == 0) {
        mode = HVAC_MODE_AUTO;
    } else if (strcmp(mode_str, "fan_only") == 0) {
        mode = HVAC_MODE_FAN_ONLY;
    } else if (strcmp(mode_str, "emergency") == 0) {
        mode = HVAC_MODE_EMERGENCY;
    } else {
        shell_error(sh, "Invalid mode: %s", mode_str);
        return -EINVAL;
    }

    int err = app_facade_hvac_set_mode(group_id, mode);
    if (err != 0) {
        shell_error(sh, "Failed to set mode (%d)", err);
        return err;
    }

    shell_print(sh, "Group %u mode set to %s", group_id, mode_str);
    return 0;
}

static int cmd_hvac_setpoint(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: hvac setpoint <grp> <cool_x10> [heat_x10]");
        return -EINVAL;
    }

    uint8_t group_id = (uint8_t)strtoul(argv[1], NULL, 10);
    int16_t cool_x10 = (int16_t)strtol(argv[2], NULL, 10);
    int16_t heat_x10 = 0;

    if (argc >= 4) {
        heat_x10 = (int16_t)strtol(argv[3], NULL, 10);
    }

    int err = app_facade_hvac_set_setpoint(group_id, cool_x10, heat_x10);
    if (err != 0) {
        shell_error(sh, "Failed to set setpoints (%d)", err);
        return err;
    }

    shell_print(sh, "Group %u setpoints updated: Cool=%.1f C, Heat=%.1f C",
                group_id, (double)cool_x10 / 10.0, (double)heat_x10 / 10.0);
    return 0;
}

static int cmd_hvac_rotate(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: hvac rotate <grp>");
        return -EINVAL;
    }

    uint8_t group_id = (uint8_t)strtoul(argv[1], NULL, 10);
    int err = app_facade_hvac_force_unit_rotation(group_id);
    if (err != 0) {
        shell_error(sh, "Failed to force unit rotation (%d)", err);
        return err;
    }

    shell_print(sh, "Forced duty rotation executed for Group %u", group_id);
    return 0;
}

static int cmd_hvac_manual(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: hvac manual <unit> <on|off> [comp_on|comp_off] [fan_on|fan_off]");
        return -EINVAL;
    }

    uint8_t unit_id = (uint8_t)strtoul(argv[1], NULL, 10);
    bool enable = (strcmp(argv[2], "on") == 0);
    bool run_comp = true;
    bool run_fan = true;

    if (argc >= 4) {
        run_comp = (strcmp(argv[3], "comp_on") == 0);
    }
    if (argc >= 5) {
        run_fan = (strcmp(argv[4], "fan_on") == 0);
    }

    int err = app_facade_hvac_set_unit_manual(unit_id, enable, run_comp, run_fan);
    if (err != 0) {
        shell_error(sh, "Failed to apply manual override (%d)", err);
        return err;
    }

    shell_print(sh, "Unit %u manual override %s (compressor=%d, fan=%d)",
                unit_id, enable ? "ENABLED" : "DISABLED", run_comp, run_fan);
    return 0;
}

static int cmd_hvac_sensors(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 4) {
        shell_error(sh, "Usage: hvac sensors <grp> <start_id> <end_id> [max|avg|min]");
        return -EINVAL;
    }

    uint8_t group_id = (uint8_t)strtoul(argv[1], NULL, 10);
    uint8_t start_id = (uint8_t)strtoul(argv[2], NULL, 10);
    uint8_t end_id = (uint8_t)strtoul(argv[3], NULL, 10);
    sensor_metric_policy_t policy = SENSOR_METRIC_MAX;

    if (argc >= 5) {
        if (strcmp(argv[4], "max") == 0) {
            policy = SENSOR_METRIC_MAX;
        } else if (strcmp(argv[4], "avg") == 0) {
            policy = SENSOR_METRIC_AVG;
        } else if (strcmp(argv[4], "min") == 0) {
            policy = SENSOR_METRIC_MIN;
        } else {
            shell_error(sh, "Invalid policy: %s (choose max, avg, or min)", argv[4]);
            return -EINVAL;
        }
    }

    int err = app_facade_hvac_configure_group_sensors(group_id, start_id, end_id, policy);
    if (err != 0) {
        shell_error(sh, "Failed to configure group sensors (%d)", err);
        return err;
    }

    shell_print(sh, "Group %u sensors set to IDs %u..%u (Policy: %s)",
                group_id, start_id, end_id, hvac_metric_policy_str((uint8_t)policy));
    return 0;
}

static int cmd_hvac_config(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: hvac config <grp> <active_turn> [unit_mask_hex]");
        return -EINVAL;
    }

    uint8_t group_id = (uint8_t)strtoul(argv[1], NULL, 10);
    uint8_t active_turn = (uint8_t)strtoul(argv[2], NULL, 10);
    uint8_t unit_mask = 0U;

    if (argc >= 4) {
        unit_mask = (uint8_t)strtoul(argv[3], NULL, 16);
    }

    int err = app_facade_hvac_configure_group_units(group_id, active_turn, unit_mask);
    if (err != 0) {
        shell_error(sh, "Failed to configure group units (%d)", err);
        return err;
    }

    shell_print(sh, "Group %u units configured: active_turn=%u, unit_mask=0x%02X",
                group_id, active_turn, unit_mask);
    return 0;
}

static int cmd_hvac_rotation(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: hvac rotation <grp> <days>");
        shell_info(sh, "Supported intervals: 7, 15, 20, 30 days (allowed range: 1..365)");
        return -EINVAL;
    }

    uint8_t group_id = (uint8_t)strtoul(argv[1], NULL, 10);
    uint16_t days = (uint16_t)strtoul(argv[2], NULL, 10);

    if (group_id >= HVAC_MAX_GROUPS) {
        shell_error(sh, "Invalid group ID: %u (max %u)", group_id, HVAC_MAX_GROUPS - 1U);
        return -EINVAL;
    }

    if (days < 1U || days > 365U) {
        shell_error(sh, "Invalid rotation interval: %u days. Supported: 7, 15, 20, 30 days (allowed range: 1..365)", days);
        return -EINVAL;
    }

    int err = app_facade_hvac_set_group_rotation(group_id, days);
    if (err != 0) {
        shell_error(sh, "Failed to set rotation interval (%d)", err);
        return err;
    }

    shell_print(sh, "Group %u rotation interval set to %u days (timer reset to 0)", group_id, days);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_hvac,
    SHELL_CMD(status, NULL, "Show group modes, temperatures, active units, relays, alarms", cmd_hvac_status),
    SHELL_CMD_ARG(mode, NULL, "Set operating mode: hvac mode <grp> <off|cool|heat|auto|fan_only|emergency>", cmd_hvac_mode, 3, 0),
    SHELL_CMD_ARG(setpoint, NULL, "Set target temperature: hvac setpoint <grp> <cool_x10> [heat_x10]", cmd_hvac_setpoint, 3, 1),
    SHELL_CMD_ARG(sensors, NULL, "Set group sensor range: hvac sensors <grp> <start> <end> [max|avg|min]", cmd_hvac_sensors, 4, 1),
    SHELL_CMD_ARG(config, NULL, "Configure group units: hvac config <grp> <active_turn> [unit_mask_hex]", cmd_hvac_config, 3, 1),
    SHELL_CMD_ARG(rotation, NULL, "Set group rotation interval: hvac rotation <grp> <days (e.g. 7, 15, 20, 30)>", cmd_hvac_rotation, 3, 0),
    SHELL_CMD_ARG(rotate, NULL, "Force unit rotation turn: hvac rotate <grp>", cmd_hvac_rotate, 2, 0),
    SHELL_CMD_ARG(manual, NULL, "Manual override: hvac manual <unit> <on|off> [comp_on|comp_off] [fan_on|fan_off]", cmd_hvac_manual, 3, 2),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(hvac, &sub_hvac, "HVAC management commands", NULL);

/* =========================================================================
 * Sensor Shell Commands
 * ========================================================================= */

static int cmd_sensor_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    app_facade_sensor_view_t view;
    int err = app_facade_sensor_get_readings(&view);
    if (err != 0) {
        shell_error(sh, "Failed to get sensor readings (%d)", err);
        return err;
    }

    shell_print(sh, "=== Temperature Sensor Probes ===");
    shell_print(sh, "\nInternal On-Board RTD Channels (0..4):");
    shell_print(sh, "Ch | Temperature | Status");
    shell_print(sh, "---+-------------+--------");
    for (uint8_t i = 0U; i < view.internal_count; i++) {
        if (view.internal_rtd_valid[i]) {
            shell_print(sh, " %u |   %6.1f C   | VALID", i, (double)view.internal_rtd_temps[i]);
        } else {
            shell_print(sh, " %u |      --.- C | OPEN / INVALID", i);
        }
    }

    shell_print(sh, "\nExternal Modbus RTD Channels (CWT-TM16PT 0..15):");
    shell_print(sh, "Ch | Temperature | Status");
    shell_print(sh, "---+-------------+--------");
    for (uint8_t i = 0U; i < view.modbus_count; i++) {
        if (view.modbus_rtd_valid[i]) {
            shell_print(sh, "%2u |   %6.1f C   | VALID", i, (double)view.modbus_rtd_temps[i]);
        } else {
            shell_print(sh, "%2u |      --.- C | OPEN / INVALID", i);
        }
    }

    return 0;
}

static int cmd_sensor_matrix(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    thermal_matrix_frame_t frame;
    int err = app_facade_sensor_get_matrix(&frame);
    if (err != 0) {
        shell_error(sh, "Failed to get sensor matrix frame (%d)", err);
        return err;
    }

    shell_print(sh, "=== Semantic Thermal Sensor Matrix ===");
    shell_print(sh, "Configured Pairs: %u | Room Probes: %u | Outdoor Probes: %u",
                frame.configured_pairs, frame.configured_room_probes, frame.configured_outdoor_probes);

    shell_print(sh, "\n--- Evaporator Paired Channels (Injection / Return) ---");
    shell_print(sh, "Pair | Unit | T_inj [C] | T_ret [C] | Delta T [C] | Status");
    shell_print(sh, "-----+------+-----------+-----------+-------------+------------");
    for (uint8_t k = 0U; k < frame.configured_pairs; k++) {
        bool inj_valid = (frame.inj_valid_mask & (1U << k)) != 0U;
        bool ret_valid = (frame.ret_valid_mask & (1U << k)) != 0U;

        const char *status;
        if (inj_valid && ret_valid) {
            status = "HEALTHY";
        } else if (!inj_valid && !ret_valid) {
            status = "BOTH OPEN";
        } else if (!inj_valid) {
            status = "INJ OPEN";
        } else {
            status = "RET OPEN";
        }

        char inj_str[12];
        char ret_str[12];
        char dt_str[12];

        if (inj_valid) {
            snprintf(inj_str, sizeof(inj_str), "%6.1f", (double)frame.t_inj[k]);
        } else {
            snprintf(inj_str, sizeof(inj_str), "  --.-");
        }

        if (ret_valid) {
            snprintf(ret_str, sizeof(ret_str), "%6.1f", (double)frame.t_ret[k]);
        } else {
            snprintf(ret_str, sizeof(ret_str), "  --.-");
        }

        if (inj_valid && ret_valid) {
            float dt = frame.t_ret[k] - frame.t_inj[k];
            snprintf(dt_str, sizeof(dt_str), "%6.1f", (double)dt);
        } else {
            snprintf(dt_str, sizeof(dt_str), "  --.-");
        }

        shell_print(sh, "  %2u |  %2u  |  %s   |  %s   |   %s    | %s",
                    k, frame.pair_hvac_unit_map[k], inj_str, ret_str, dt_str, status);
    }

    shell_print(sh, "\n--- Room Ambient Probes ---");
    shell_print(sh, "Probe | Temperature | Status");
    shell_print(sh, "------+-------------+--------");
    for (uint8_t m = 0U; m < frame.configured_room_probes; m++) {
        bool valid = (frame.room_valid_mask & (1U << m)) != 0U;
        if (valid) {
            shell_print(sh, "  %2u  |   %6.1f C   | HEALTHY", m, (double)frame.t_room[m]);
        } else {
            shell_print(sh, "  %2u  |      --.- C | OPEN / INVALID", m);
        }
    }

    shell_print(sh, "\n--- Outdoor Ambient Probes ---");
    shell_print(sh, "Probe | Temperature | Status");
    shell_print(sh, "------+-------------+--------");
    for (uint8_t l = 0U; l < frame.configured_outdoor_probes; l++) {
        bool valid = (frame.outdoor_valid_mask & (1U << l)) != 0U;
        if (valid) {
            shell_print(sh, "  %2u  |   %6.1f C   | HEALTHY", l, (double)frame.t_outdoor[l]);
        } else {
            shell_print(sh, "  %2u  |      --.- C | OPEN / INVALID", l);
        }
    }

    shell_print(sh, "\n--- Psychrometric Channels ---");
    if (frame.humidity_return_valid) {
        shell_print(sh, "Return Relative Humidity: %6.1f %% (HEALTHY)", (double)frame.humidity_return_pct);
    } else {
        shell_print(sh, "Return Relative Humidity:   --.- %% (OPEN / INVALID)");
    }

    return 0;
}

static int cmd_sensor_snapshot(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    sensor_snapshot_msg_t snap;
    int err = app_facade_sensor_get_snapshot(&snap);
    if (err != 0) {
        shell_error(sh, "Failed to get sensor snapshot (%d)", err);
        return err;
    }

    shell_print(sh, "=== Flat Sensor Telemetry Snapshot (Zbus chan_sensor_snapshot) ===");
    shell_print(sh, "Seq: %u | Timestamp: %llu ms | Valid Mask: 0x%016llX\n",
                snap.sequence, (unsigned long long)snap.timestamp_ms,
                (unsigned long long)snap.valid_mask);

    shell_print(sh, "Flat ID | Channel Name             | Value      | Status");
    shell_print(sh, "--------+--------------------------+------------+--------");

    for (uint8_t i = 0U; i < 5U; i++) {
        uint64_t bit = 1ULL << i;
        bool valid = (snap.valid_mask & bit) != 0ULL;
        if (valid) {
            shell_print(sh, "   %2u   | Internal RTD %u           | %6.1f degC | VALID",
                        i + 1U, i, (double)snap.temperatures[i]);
        } else {
            shell_print(sh, "   %2u   | Internal RTD %u           |   --.- degC | INVALID",
                        i + 1U, i);
        }
    }

    for (uint8_t c = 0U; c < 16U; c++) {
        uint8_t idx = 9U + c;
        uint64_t bit = 1ULL << idx;
        bool valid = (snap.valid_mask & bit) != 0ULL;
        if (valid) {
            shell_print(sh, "   %2u   | Modbus CWT1 CH %2u       | %6.1f degC | VALID",
                        10U + c, c + 1U, (double)snap.temperatures[idx]);
        } else {
            shell_print(sh, "   %2u   | Modbus CWT1 CH %2u       |   --.- degC | INVALID",
                        10U + c, c + 1U);
        }
    }

    for (uint8_t c = 0U; c < 16U; c++) {
        uint8_t idx = 29U + c;
        uint64_t bit = 1ULL << idx;
        bool valid = (snap.valid_mask & bit) != 0ULL;
        if (valid) {
            shell_print(sh, "   %2u   | Modbus CWT2 CH %2u       | %6.1f degC | VALID",
                        30U + c, c + 1U, (double)snap.temperatures[idx]);
        } else {
            shell_print(sh, "   %2u   | Modbus CWT2 CH %2u       |   --.- degC | INVALID",
                        30U + c, c + 1U);
        }
    }

    for (uint8_t j = 0U; j < 10U; j++) {
        uint8_t idx = 49U + j;
        uint64_t bit = 1ULL << idx;
        bool valid = (snap.valid_mask & bit) != 0ULL;
        if (valid) {
            shell_print(sh, "   %2u   | Analog J9 CH %2u         |  %5.2f mA  | VALID",
                        50U + j, j + 1U, (double)snap.temperatures[idx]);
        } else {
            shell_print(sh, "   %2u   | Analog J9 CH %2u         |   --.-- mA | INVALID",
                        50U + j, j + 1U);
        }
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sensor,
    SHELL_CMD(matrix, NULL, "Display semantic sensor matrix (evaporator pairs, ambient, RH)", cmd_sensor_matrix),
    SHELL_CMD(snapshot, NULL, "Display flat 64-channel Zbus telemetry snapshot", cmd_sensor_snapshot),
    SHELL_CMD(status, NULL, "Display all RTD channels (internal 0..4, external Modbus)", cmd_sensor_status),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sensor, &sub_sensor, "Sensor subsystem commands", NULL);

/* =========================================================================
 * Digital Input Shell Commands
 * ========================================================================= */

static int cmd_di_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    di_snapshot_msg_t snap;
    int err = app_facade_di_get_snapshot(&snap);
    if (err != 0) {
        shell_error(sh, "Failed to get DI status (%d)", err);
        return err;
    }

    shell_print(sh, "=== Digital Inputs & Contact Alarms Telemetry ===");
    shell_print(sh, "Total Channels: %u | Raw Mask: 0x%016llX | Active Mask: 0x%016llX\n",
                snap.channel_count, (unsigned long long)snap.raw_mask,
                (unsigned long long)snap.active_mask);

    shell_print(sh, "Idx | Raw | State    | Duration (s) | Semantic Role & Signal Name");
    shell_print(sh, "----+-----+----------+--------------+----------------------------------");
    for (uint8_t i = 0U; i < snap.channel_count && i < DI_CHANNEL_MAX; i++) {
        bool raw_level = ((snap.raw_mask & (1ULL << i)) != 0ULL);
        const char *name = app_facade_di_role_name(snap.roles[i]);
        shell_print(sh, "%3u |  %c  | %-8s | %12u | %s",
                    i,
                    raw_level ? '1' : '0',
                    snap.is_active[i] ? "ACTIVE" : "INACTIVE",
                    snap.active_duration_s[i],
                    name != NULL ? name : "Unknown");
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_di,
    SHELL_CMD(status, NULL, "Display state of 24 internal + 32 external DI channels and named roles", cmd_di_status),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(di, &sub_di, "Digital Input subsystem commands", NULL);

/* =========================================================================
 * Modbus Shell Commands
 * ========================================================================= */

static int cmd_modbus_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    app_facade_modbus_view_t view;
    int err = app_facade_modbus_get_status(&view);
    if (err != 0) {
        shell_error(sh, "Failed to get Modbus status (%d)", err);
        return err;
    }

    shell_print(sh, "=== Modbus Master Transceiver & Buses ===");
    for (uint8_t b = 0U; b < MODBUS_BUS_MAX; b++) {
        shell_print(sh, "Bus %u (RS485-%u): Baud=%u, Parity=%s, StopBits=%u | %s | Slaves=%zu",
                    b, b + 1U, view.buses[b].baudrate, modbus_parity_str(view.buses[b].parity),
                    view.buses[b].stop_bits, view.buses[b].is_online ? "ONLINE" : "OFFLINE",
                    view.buses[b].slave_count);
    }

    shell_print(sh, "\n=== Polled Slave Devices (%zu total) ===", view.total_slaves);
    shell_print(sh, "DevID | Bus | Addr | Type | Health    | Total Tx | Success | Timeouts | CRC Errs | Exceptions");
    shell_print(sh, "------+-----+------+------+-----------+----------+---------+----------+----------+-----------");
    for (size_t s = 0U; s < view.total_slaves; s++) {
        shell_print(sh, "  %3u |   %u |  %3u |  %3u | %-9s | %8u | %7u | %8u | %8u | %10u",
                    view.slaves[s].device_id,
                    view.slaves[s].bus_id,
                    view.slaves[s].slave_addr,
                    view.slaves[s].dev_type,
                    modbus_health_str(view.slaves[s].health),
                    view.slaves[s].total_transactions,
                    view.slaves[s].successful_transactions,
                    view.slaves[s].timeouts,
                    view.slaves[s].crc_errors,
                    view.slaves[s].exceptions);
    }

    return 0;
}

static int cmd_modbus_slave(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_print(sh, "Usage: modbus slave <id> <addr> [bus] [type] [poll_ms] [timeout_ms]");
        return -EINVAL;
    }

    uint8_t dev_id = (uint8_t)strtoul(argv[1], NULL, 0);
    uint8_t slave_addr = (uint8_t)strtoul(argv[2], NULL, 0);

    modbus_slave_descriptor_t desc;
    int err = app_facade_modbus_get_slave(dev_id, &desc);
    if (err == 0 && argc == 3) {
        desc.slave_addr = slave_addr;
    } else {
        desc.device_id = dev_id;
        desc.slave_addr = slave_addr;
        desc.bus_id = (argc > 3) ? (uint8_t)strtoul(argv[3], NULL, 0) : 0U;
        desc.dev_type = (argc > 4) ? (modbus_device_type_t)strtoul(argv[4], NULL, 0) : MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH;
        desc.poll_interval_ms = (argc > 5) ? (uint16_t)strtoul(argv[5], NULL, 0) : 500U;
        desc.timeout_ms = (argc > 6) ? (uint16_t)strtoul(argv[6], NULL, 0) : 300U;
        desc.is_enabled = true;
    }

    err = app_facade_modbus_configure_slave(&desc);
    if (err != 0) {
        shell_error(sh, "Failed to configure slave %u: %d", dev_id, err);
        return err;
    }

    shell_print(sh, "Slave %u configured: Bus %u, Addr %u, Type %u, Poll %u ms, Timeout %u ms (saved to EEPROM)",
                desc.device_id, desc.bus_id, desc.slave_addr, desc.dev_type, desc.poll_interval_ms, desc.timeout_ms);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_modbus,
    SHELL_CMD(status, NULL, "Display status of active RS485 buses, polling stats, slave list", cmd_modbus_status),
    SHELL_CMD_ARG(slave, NULL, "Configure Modbus slave device address and parameters", cmd_modbus_slave, 3, 4),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(modbus, &sub_modbus, "Modbus master subsystem commands", NULL);

/* =========================================================================
 * Storage Shell Commands
 * ========================================================================= */

static int cmd_storage_save(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int count = app_facade_storage_save_all();
    if (count < 0) {
        shell_error(sh, "Storage save failed: %d", count);
        return count;
    }

    shell_print(sh, "Persisted %d configuration domain(s) to EEPROM successfully", count);
    return 0;
}

static int cmd_storage_reload(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = app_facade_storage_reload_all();
    if (err != 0) {
        shell_error(sh, "Storage reload failed: %d", err);
        return err;
    }

    shell_print(sh, "Configuration reloaded from EEPROM");
    return 0;
}

static int cmd_storage_reset(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = app_facade_storage_reset_defaults();
    if (err != 0) {
        shell_error(sh, "Storage factory reset failed: %d", err);
        return err;
    }

    shell_print(sh, "EEPROM reset to factory defaults and default profile reapplied");
    return 0;
}

static int cmd_storage_stats(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    storage_stats_t stats;
    int err = app_facade_storage_get_stats(&stats);
    if (err != 0) {
        shell_error(sh, "Failed to get storage stats: %d", err);
        return err;
    }

    shell_print(sh, "=== Persistent Storage Engine Statistics ===");
    shell_print(sh, "  Persisted Writes:  %u", stats.write_count);
    shell_print(sh, "  Storage Reads:     %u", stats.read_count);
    shell_print(sh, "  Domains Committed: %u", stats.domains_persisted);
    shell_print(sh, "  CRC Check Errors:  %u", stats.crc_errors);
    shell_print(sh, "  Write Faults:      %u", stats.write_errors);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_storage,
    SHELL_CMD(save, NULL, "Commit dirty configs to EEPROM immediately", cmd_storage_save),
    SHELL_CMD(reload, NULL, "Reload all domains from storage", cmd_storage_reload),
    SHELL_CMD(reset, NULL, "Reset EEPROM to factory defaults", cmd_storage_reset),
    SHELL_CMD(stats, NULL, "Show storage transaction counters, CRC errors, write cycles", cmd_storage_stats),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(storage, &sub_storage, "Storage subsystem commands", NULL);

/* =========================================================================
 * System / Diagnostics Shell Commands
 * ========================================================================= */

static int cmd_sys_info(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    app_facade_system_info_t info;
    int err = app_facade_system_get_info(&info);
    if (err != 0) {
        shell_error(sh, "Failed to get system info: %d", err);
        return err;
    }

    uint32_t hours = (uint32_t)(info.uptime_seconds / 3600ULL);
    uint32_t mins = (uint32_t)((info.uptime_seconds % 3600ULL) / 60ULL);
    uint32_t secs = (uint32_t)(info.uptime_seconds % 60ULL);

    shell_print(sh, "=== BrightC Telemetry Controller System Info ===");
    shell_print(sh, "  Firmware Version: %s", info.firmware_version);
    shell_print(sh, "  Active Profile:   %s", info.profile_name);
    shell_print(sh, "  Uptime:           %u h %u m %u s (%llu seconds)",
                hours, mins, secs, (unsigned long long)info.uptime_seconds);
    shell_print(sh, "  Heap Memory Pool: %zu bytes", info.total_heap_bytes);
    return 0;
}

static int cmd_sys_reboot(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_warn(sh, "Rebooting BrightC Telemetry Controller now...");
    (void)app_facade_system_reboot();
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sys,
    SHELL_CMD(info, NULL, "Firmware version, uptime, heap/stack status, profile name", cmd_sys_info),
    SHELL_CMD(reboot, NULL, "Software reset", cmd_sys_reboot),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sys, &sub_sys, "System diagnostics commands", NULL);
