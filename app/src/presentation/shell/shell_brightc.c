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
    case GROUP_STATE_OFF:        return "OFF";
    case GROUP_STATE_IDLE:       return "IDLE";
    case GROUP_STATE_COOLING:    return "COOLING";
    case GROUP_STATE_COOL_BOOST: return "COOL_BOOST";
    case GROUP_STATE_BYPASS:     return "BYPASS";
    case GROUP_STATE_FIRE:       return "FIRE_SHUTDOWN";
    case GROUP_STATE_MANUAL:     return "MANUAL";
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

static const char *alarm_level_str(alarm_level_t level)
{
    switch (level) {
    case ALARM_LEVEL_NONE:     return "NONE";
    case ALARM_LEVEL_INFO:     return "INFO";
    case ALARM_LEVEL_WARNING:  return "WARNING";
    case ALARM_LEVEL_CRITICAL: return "CRITICAL";
    default:                   return "UNKNOWN";
    }
}

static const char *alarm_state_str(alarm_state_t state)
{
    switch (state) {
    case ALARM_STATE_INACTIVE:     return "INACTIVE";
    case ALARM_STATE_ACTIVE:       return "ACTIVE";
    case ALARM_STATE_ACKNOWLEDGED: return "ACKNOWLEDGED";
    case ALARM_STATE_LATCHED:      return "LATCHED";
    default:                       return "UNKNOWN";
    }
}

static const char *alarm_category_str(alarm_category_t cat)
{
    switch (cat) {
    case ALARM_SOURCE_DIGITAL_INPUT: return "DIGITAL_IN";
    case ALARM_SOURCE_SENSOR_ANALOG: return "ANALOG_SENSOR";
    case ALARM_SOURCE_ELECTRICAL:    return "ELECTRICAL";
    case ALARM_SOURCE_HVAC_CONTROL:  return "HVAC_CONTROL";
    case ALARM_SOURCE_SYSTEM_INFRA:  return "SYSTEM_INFRA";
    case ALARM_SOURCE_CUSTOM:        return "CUSTOM";
    default:                         return "UNKNOWN";
    }
}

static const char *thermal_quality_str(quality_status_t q)
{
    switch (q) {
    case QUALITY_VALID:       return "VALID";
    case QUALITY_SUSPECT:     return "SUSPECT";
    case QUALITY_FAILED:      return "FAILED";
    case QUALITY_UNAVAILABLE: return "UNAVAILABLE";
    default:                  return "UNKNOWN";
    }
}

static const char *compressor_fsm_state_str(compressor_fsm_state_t state)
{
    switch (state) {
    case COMPRESSOR_STATE_OFF:               return "OFF";
    case COMPRESSOR_STATE_INRUSH_WAIT:       return "INRUSH_WAIT";
    case COMPRESSOR_STATE_STEP_LOAD_CAPTURE: return "STEP_LOAD_CAPTURE";
    case COMPRESSOR_STATE_THERMAL_WARMUP:    return "THERMAL_WARMUP";
    case COMPRESSOR_STATE_STEADY_STATE:      return "STEADY_STATE";
    default:                                 return "UNKNOWN";
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
        shell_print(sh, "  Fan Control:  %s (purge delay=%u s)",
                    status.groups[g].enable_fan_delay ? "ON (cut fan when AC off)" : "OFF (fan always enabled)",
                    status.groups[g].fan_delay_s);
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
                    hvac_tri_state_str(status.units[u].tri_state),
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

static int cmd_hvac_fan_ctrl(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: hvac fan_ctrl <grp> <on|off> [fan_delay_s]");
        return -EINVAL;
    }

    uint8_t group_id = (uint8_t)strtoul(argv[1], NULL, 10);
    bool enable;
    if (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "1") == 0) {
        enable = true;
    } else if (strcmp(argv[2], "off") == 0 || strcmp(argv[2], "0") == 0) {
        enable = false;
    } else {
        shell_error(sh, "Invalid fan_ctrl mode: %s (expected on or off)", argv[2]);
        return -EINVAL;
    }

    uint16_t fan_delay_s = 0U;
    if (argc >= 4) {
        fan_delay_s = (uint16_t)strtoul(argv[3], NULL, 10);
    }

    int err = app_facade_hvac_set_group_fan_control(group_id, enable, fan_delay_s);
    if (err != 0) {
        shell_error(sh, "Failed to configure fan control (%d)", err);
        return err;
    }

    shell_print(sh, "Group %u fan control (Relay G) set to %s",
                group_id,
                enable ? "ON (cut fan when AC off after post-purge)" : "OFF (fan always enabled)");
    return 0;
}

static int cmd_hvac_relay(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: hvac relay <ch 0..17> <0|1>");
        return -EINVAL;
    }

    uint8_t ch = (uint8_t)strtoul(argv[1], NULL, 10);
    if (ch > 17U) {
        shell_error(sh, "Invalid relay channel: %u (must be 0..17)", ch);
        return -EINVAL;
    }

    bool state = (strtoul(argv[2], NULL, 10) != 0U);
    int err = app_facade_hvac_test_relay(ch, state);
    if (err != 0) {
        shell_error(sh, "Failed to actuate relay %u (%d)", ch, err);
        return err;
    }

    shell_print(sh, "Relay %u -> %s", ch, state ? "ON (CLOSED)" : "OFF (OPEN)");
    return 0;
}

static int cmd_hvac_set_temp(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: hvac set_temp <degrees_c>");
        return -EINVAL;
    }

    float temp = strtof(argv[1], NULL);
    int err = app_facade_hvac_set_test_temp(temp);
    if (err != 0) {
        shell_error(sh, "Failed to set test temperature (%d)", err);
        return err;
    }

    shell_print(sh, "Injected HVAC test temperature: %.1f C", (double)temp);
    return 0;
}

static int cmd_hvac_clear_temp(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = app_facade_hvac_clear_test_temp();
    if (err != 0) {
        shell_error(sh, "Failed to clear test temperature (%d)", err);
        return err;
    }

    shell_print(sh, "Cleared HVAC test temperature override. Resumed live sensors.");
    return 0;
}

static int cmd_hvac_eeprom_reset(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Resetting HVAC configuration in EEPROM to default 26/28/30/32 setpoints...");
    int err = app_facade_hvac_reset_eeprom();
    if (err != 0) {
        shell_error(sh, "Failed to reset HVAC EEPROM (%d)", err);
        return err;
    }

    shell_print(sh, "HVAC EEPROM reset and profile applied successfully.");
    return 0;
}

static int cmd_hvac_timers(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "=== HVAC TIMERS & COUNTDOWNS ===");
    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        uint32_t cooling_timer_s = 0U;
        uint32_t rotation_timer_s = 0U;
        uint16_t startup_timers[HVAC_MAX_UNITS_TOTAL] = {0};
        uint16_t fan_timers[HVAC_MAX_UNITS_TOTAL] = {0};

        int err = app_facade_hvac_get_timers(g, &cooling_timer_s, &rotation_timer_s,
                                             startup_timers, fan_timers);
        if (err != 0) {
            continue;
        }

        shell_print(sh, "[Group %u] Cooling timer: %u s | Rotation timer: %u s (%u d %02u:%02u:%02u)",
                    g, cooling_timer_s, rotation_timer_s,
                    rotation_timer_s / 86400U,
                    (rotation_timer_s % 86400U) / 3600U,
                    (rotation_timer_s % 3600U) / 60U,
                    rotation_timer_s % 60U);

        for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
            shell_print(sh, "  Unit %u: Startup Delay = %u s | Fan Purge Delay = %u s",
                        u, startup_timers[u], fan_timers[u]);
        }
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_hvac,
    SHELL_CMD(status, NULL, "Show group modes, temperatures, active units, relays, alarms", cmd_hvac_status),
    SHELL_CMD_ARG(mode, NULL, "Set operating mode: hvac mode <grp> <off|cool|heat|auto|fan_only|emergency>", cmd_hvac_mode, 3, 0),
    SHELL_CMD_ARG(setpoint, NULL, "Set target temperature: hvac setpoint <grp> <cool_x10> [heat_x10]", cmd_hvac_setpoint, 3, 1),
    SHELL_CMD_ARG(sensors, NULL, "Set group sensor range: hvac sensors <grp> <start> <end> [max|avg|min]", cmd_hvac_sensors, 4, 1),
    SHELL_CMD_ARG(config, NULL, "Configure group units: hvac config <grp> <active_turn> [unit_mask_hex]", cmd_hvac_config, 3, 1),
    SHELL_CMD_ARG(rotation, NULL, "Set group rotation interval: hvac rotation <grp> <days (e.g. 7, 15, 20, 30)>", cmd_hvac_rotation, 3, 0),
    SHELL_CMD_ARG(fan_ctrl, NULL, "Configure Relay G fan control: hvac fan_ctrl <grp> <on|off> [delay_s]", cmd_hvac_fan_ctrl, 3, 1),
    SHELL_CMD_ARG(rotate, NULL, "Force unit rotation turn: hvac rotate <grp>", cmd_hvac_rotate, 2, 0),
    SHELL_CMD_ARG(manual, NULL, "Manual override: hvac manual <unit> <on|off> [comp_on|comp_off] [fan_on|fan_off]", cmd_hvac_manual, 3, 2),
    SHELL_CMD_ARG(relay, NULL, "Directly drive relay: hvac relay <ch 0..17> <0|1>", cmd_hvac_relay, 3, 0),
    SHELL_CMD_ARG(set_temp, NULL, "Inject test temperature: hvac set_temp <degrees_c>", cmd_hvac_set_temp, 2, 0),
    SHELL_CMD(clear_temp, NULL, "Clear injected test temperature and resume sensors", cmd_hvac_clear_temp),
    SHELL_CMD(eeprom_reset, NULL, "Reset HVAC domain in EEPROM to default 26/28/30/32 setpoints and apply", cmd_hvac_eeprom_reset),
    SHELL_CMD(timers, NULL, "Show active countdown timers for all units and groups", cmd_hvac_timers),
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
    shell_print(sh, "Ch | Raw Counts | Resistance | Temperature | Status");
    shell_print(sh, "---+------------+------------+-------------+----------------------------------");
    for (uint8_t i = 0U; i < view.internal_count; i++) {
        if (view.internal_rtd_valid[i]) {
            shell_print(sh, " %u |   %8.0f |  %7.1f Ω |   %6.1f C   | VALID",
                        i,
                        (double)view.internal_rtd_raw_counts[i],
                        (double)view.internal_rtd_ohms[i],
                        (double)view.internal_rtd_temps[i]);
        } else {
            shell_print(sh, " %u |   %8.0f |  %7.1f Ω |   %6.1f C   | OPEN / INVALID (R=%.1f Ω, T=%.1f C)",
                        i,
                        (double)view.internal_rtd_raw_counts[i],
                        (double)view.internal_rtd_ohms[i],
                        (double)view.internal_rtd_temps[i],
                        (double)view.internal_rtd_ohms[i],
                        (double)view.internal_rtd_temps[i]);
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

static int cmd_sensor_raw(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    raw_sensor_readings_t raw;
    int err = app_facade_sensor_get_raw(&raw);
    if (err != 0) {
        shell_error(sh, "Failed to get raw ADC readings (%d)", err);
        return err;
    }

    shell_print(sh, "=== Raw Logical ADC Channels (0..14) ===");
    shell_print(sh, "Logical | Physical Ch | Name              | Raw Counts | Voltage (V)");
    shell_print(sh, "--------+-------------+-------------------+------------+------------");

    for (uint8_t i = 0U; i < 15U; i++) {
        uint8_t phys_ch;
        const char *name;
        float counts = 0.0f;

        if (i < 5U) {
            phys_ch = (uint8_t)(PHYS_CH_INTERNAL_RTD_1 + i);
            counts = raw.internal_rtd_raw_adc[i];
            static const char * const rtd_names[5] = {
                "INTERNAL_RTD_1",
                "INTERNAL_RTD_2",
                "INTERNAL_RTD_3",
                "INTERNAL_RTD_4",
                "INTERNAL_RTD_5",
            };
            name = rtd_names[i];
        } else {
            uint8_t j9_idx = i - 5U;
            phys_ch = (uint8_t)(PHYS_CH_ANALOG_J9_CH1 + j9_idx);
            counts = raw.analog_j9_raw_adc[j9_idx];
            static const char * const j9_names[10] = {
                "ANALOG_J9_CH1",
                "ANALOG_J9_CH2",
                "ANALOG_J9_CH3",
                "ANALOG_J9_CH4",
                "ANALOG_J9_CH5",
                "ANALOG_J9_CH6",
                "ANALOG_J9_CH7",
                "ANALOG_J9_CH8",
                "ANALOG_J9_CH9",
                "ANALOG_J9_CH10",
            };
            name = j9_names[j9_idx];
        }

        float v_adc = (counts * 3.3f) / 65535.0f;
        shell_print(sh, "   %2u   |     %2u      | %-17s |   %8.0f |   %6.4f V",
                    i, phys_ch, name, (double)counts, (double)v_adc);
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sensor,
    SHELL_CMD(matrix, NULL, "Display semantic sensor matrix (evaporator pairs, ambient, RH)", cmd_sensor_matrix),
    SHELL_CMD(raw, NULL, "Display raw logical ADC channels (0..14)", cmd_sensor_raw),
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

/* =========================================================================
 * Alarm Management Shell Commands
 * ========================================================================= */

static int cmd_alarm_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    app_facade_alarm_status_t status;
    int err = app_facade_alarm_get_status(&status);
    if (err != 0) {
        shell_error(sh, "Failed to retrieve alarm status: %d", err);
        return err;
    }

    shell_print(sh, "=== Alarm Subsystem Status ===");
    shell_print(sh, "  Active Alarms:      %zu", status.active_count);
    shell_print(sh, "  Highest Severity:   %s", alarm_level_str(status.highest_level));
    shell_print(sh, "  Acknowledged Count: %zu", status.acknowledged_count);
    shell_print(sh, "  Total Registered:   %zu", status.total_registered);
    shell_print(sh, "  Alarm Buzzer:       %s", status.buzzer_muted ? "MUTED" : "AUDIBLE");

    return 0;
}

static const alarm_descriptor_t *s_alarm_catalog_buf[CONFIG_ALARM_MANAGER_MAX_ALARMS];

static int cmd_alarm_list(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    size_t total = app_facade_alarm_get_catalog(s_alarm_catalog_buf, ARRAY_SIZE(s_alarm_catalog_buf));
    bool header_printed = false;

    for (size_t i = 0U; i < total; i++) {
        const alarm_descriptor_t *desc = s_alarm_catalog_buf[i];
        if (desc == NULL) {
            continue;
        }

        alarm_snapshot_entry_t snap = {0};
        if (app_facade_alarm_get_snapshot(desc->alarm_id, &snap, NULL) != 0) {
            continue;
        }
        if (snap.state == ALARM_STATE_INACTIVE) {
            continue;
        }

        if (!header_printed) {
            shell_print(sh, "+--------+----------------------+----------+---------------+----------+------------+-------+");
            shell_print(sh, "| ID     | Name                 | Level    | Category      | Value    | Active(s)  | Acked |");
            shell_print(sh, "+--------+----------------------+----------+---------------+----------+------------+-------+");
            header_printed = true;
        }

        alarm_level_t level = ALARM_LEVEL_INFO;
        if (snap.severity == ALARM_SEVERITY_FATAL || snap.severity == ALARM_SEVERITY_CRITICAL) {
            level = ALARM_LEVEL_CRITICAL;
        } else if (snap.severity == ALARM_SEVERITY_WARNING) {
            level = ALARM_LEVEL_WARNING;
        }

        uint32_t active_s = snap.active_duration_ms / 1000U;
        shell_print(sh, "| 0x%04X | %-20.20s | %-8s | %-13s | %8.2f | %10u | %-5s |",
                    desc->alarm_id,
                    desc->name ? desc->name : "UNKNOWN",
                    alarm_level_str(level),
                    alarm_category_str(desc->source_category),
                    (double)snap.last_trigger_value,
                    active_s,
                    (snap.state == ALARM_STATE_ACKNOWLEDGED) ? "YES" : "NO");
    }

    if (!header_printed) {
        shell_print(sh, "No active alarms.");
        return 0;
    }

    shell_print(sh, "+--------+----------------------+----------+---------------+----------+------------+-------+");

    return 0;
}

static int cmd_alarm_catalog(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    size_t count = app_facade_alarm_get_catalog(s_alarm_catalog_buf, ARRAY_SIZE(s_alarm_catalog_buf));

    shell_print(sh, "=== Registered Alarm Catalog (%zu descriptors) ===", count);
    shell_print(sh, "+--------+----------------+---------------+----------+-------+-------+-------+--------------------------------+");
    shell_print(sh, "| ID     | Name           | Category      | Severity | Latch | Ack   | Agg   | Description                    |");
    shell_print(sh, "+--------+----------------+---------------+----------+-------+-------+-------+--------------------------------+");

    for (size_t i = 0U; i < count; i++) {
        const alarm_descriptor_t *desc = s_alarm_catalog_buf[i];
        if (desc == NULL) {
            continue;
        }

        const char *sev_str = "INFO";
        if (desc->default_severity == ALARM_SEVERITY_FATAL || desc->default_severity == ALARM_SEVERITY_CRITICAL) {
            sev_str = "CRITICAL";
        } else if (desc->default_severity == ALARM_SEVERITY_WARNING) {
            sev_str = "WARNING";
        }

        shell_print(sh, "| 0x%04X | %-14.14s | %-13s | %-8s | %-5s | %-5s | %-5s | %-30.30s |",
                    desc->alarm_id,
                    desc->name ? desc->name : "",
                    alarm_category_str(desc->source_category),
                    sev_str,
                    desc->is_latch_supported ? "YES" : "NO",
                    desc->is_ack_supported ? "YES" : "NO",
                    desc->is_aggregated ? "YES" : "NO",
                    desc->description ? desc->description : "");
    }
    shell_print(sh, "+--------+----------------+---------------+----------+-------+-------+-------+--------------------------------+");

    return 0;
}

static int cmd_alarm_snapshot(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: alarm snapshot <id> (e.g. 0x0001 or 1)");
        return -EINVAL;
    }

    char *endptr = NULL;
    unsigned long val = strtoul(argv[1], &endptr, 0);
    if (endptr == argv[1] || val > 0xFFFFU) {
        shell_error(sh, "Invalid alarm ID: %s", argv[1]);
        return -EINVAL;
    }
    uint16_t id = (uint16_t)val;

    alarm_snapshot_entry_t snap = {0};
    const alarm_descriptor_t *desc = NULL;
    int err = app_facade_alarm_get_snapshot(id, &snap, &desc);
    if (err != 0) {
        shell_error(sh, "Failed to get snapshot for alarm 0x%04X: %d", id, err);
        return err;
    }

    shell_print(sh, "=== Alarm Snapshot [0x%04X: %s] ===", id, desc ? desc->name : "UNKNOWN");
    if (desc != NULL && desc->description != NULL) {
        shell_print(sh, "  Description:     %s", desc->description);
        shell_print(sh, "  Category:        %s", alarm_category_str(desc->source_category));
    }
    shell_print(sh, "  State:           %s", alarm_state_str(snap.state));
    shell_print(sh, "  Severity:        %u", (unsigned int)snap.severity);
    shell_print(sh, "  Trigger Count:   %u", snap.trigger_count);
    shell_print(sh, "  Trigger Value:   %.2f", (double)snap.last_trigger_value);
    shell_print(sh, "  Active Duration: %u ms", snap.active_duration_ms);
    shell_print(sh, "  Sources Mask:    0x%08X", snap.contributing_sources_mask);
    shell_print(sh, "  Triggered At:    %llu ms uptime", (unsigned long long)snap.triggered_timestamp_ms);
    shell_print(sh, "  Cleared At:      %llu ms uptime", (unsigned long long)snap.cleared_timestamp_ms);

    return 0;
}

static int cmd_alarm_ack(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: alarm ack <id> (e.g. 0x0001 or 1)");
        return -EINVAL;
    }

    char *endptr = NULL;
    unsigned long val = strtoul(argv[1], &endptr, 0);
    if (endptr == argv[1] || val > 0xFFFFU) {
        shell_error(sh, "Invalid alarm ID: %s", argv[1]);
        return -EINVAL;
    }
    uint16_t id = (uint16_t)val;

    int err = app_facade_alarm_ack(id);
    if (err != 0) {
        shell_error(sh, "Failed to acknowledge alarm 0x%04X: %d", id, err);
        return err;
    }

    shell_print(sh, "Alarm 0x%04X acknowledged successfully.", id);
    return 0;
}

static int cmd_alarm_mute(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        bool muted = app_facade_alarm_is_buzzer_muted();
        shell_print(sh, "Alarm buzzer is currently: %s", muted ? "MUTED" : "AUDIBLE");
        return 0;
    }

    bool new_mute = false;
    if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0 || strcmp(argv[1], "true") == 0) {
        new_mute = true;
    } else if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0 || strcmp(argv[1], "false") == 0) {
        new_mute = false;
    } else if (strcmp(argv[1], "toggle") == 0) {
        new_mute = !app_facade_alarm_is_buzzer_muted();
    } else {
        shell_error(sh, "Usage: alarm mute [on|off|toggle]");
        return -EINVAL;
    }

    int err = app_facade_alarm_set_buzzer_mute(new_mute);
    if (err != 0) {
        shell_error(sh, "Failed to update alarm buzzer mute state: %d", err);
        return err;
    }

    shell_print(sh, "Alarm buzzer mute set to %s (persisted to EEPROM).",
                new_mute ? "MUTED" : "AUDIBLE");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_alarm,
    SHELL_CMD(status, NULL, "Overall alarm subsystem status", cmd_alarm_status),
    SHELL_CMD(list, NULL, "List active and acknowledged alarms", cmd_alarm_list),
    SHELL_CMD(active, NULL, "Alias for alarm list", cmd_alarm_list),
    SHELL_CMD(catalog, NULL, "Catalog of all registered alarm descriptors", cmd_alarm_catalog),
    SHELL_CMD_ARG(snapshot, NULL, "Detailed snapshot for alarm ID: alarm snapshot <id>", cmd_alarm_snapshot, 2, 0),
    SHELL_CMD_ARG(ack, NULL, "Acknowledge active alarm: alarm ack <id>", cmd_alarm_ack, 2, 0),
    SHELL_CMD_ARG(mute, NULL, "Query or set alarm buzzer mute: alarm mute [on|off|toggle]", cmd_alarm_mute, 1, 1),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(alarm, &sub_alarm, "Alarm management subsystem commands", NULL);

/* =========================================================================
 * Thermal and Energy Subsystem Shell Commands
 * ========================================================================= */

static int cmd_thermal_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    thermal_energy_result_t res;
    int err = app_facade_thermal_get_result(&res);
    if (err != 0) {
        shell_error(sh, "Failed to get thermal result: %d", err);
        return err;
    }

    thermal_energy_capture_t cap;
    (void)app_facade_thermal_get_capture(&cap);

    config_thermal_baseline_t base;
    (void)app_facade_thermal_get_baseline(&base);

    shell_print(sh, "=== Thermal & Energy System Status ===");
    shell_print(sh, "  Overall Quality:     %s", thermal_quality_str(res.overall_quality));
    shell_print(sh, "  Generation ID:       %u", res.generation_id);
    shell_print(sh, "  Calculation Time:    %u us", res.calculation_duration_us);

    shell_print(sh, "--- Psychrometrics & Airflow ---");
    shell_print(sh, "  P_atm:               %.2f kPa", (double)res.p_atm_kpa);
    shell_print(sh, "  T_ret_mean:          %.2f degC", (double)res.t_ret_mean);
    shell_print(sh, "  T_inj_mean:          %.2f degC", (double)res.t_inj_mean);
    shell_print(sh, "  Delta T_clima:       %.2f degC", (double)res.delta_t_clima_mean);
    shell_print(sh, "  HR_ret:              %.1f %%", (double)cap.humidity_return_pct);
    shell_print(sh, "  omega_ret:           %.4f kg/kg", (double)res.humidity_ratio_ret);
    shell_print(sh, "  T_rocio:             %.2f degC", (double)res.t_rocio_ret);
    shell_print(sh, "  Air Mass Flow (m_da):%.3f kg/s", (double)res.m_dot_da_total);

    shell_print(sh, "--- Room Ambient ---");
    shell_print(sh, "  T_sala_mean:         %.2f degC", (double)res.t_sala_mean);
    shell_print(sh, "  T_sala_max:          %.2f degC", (double)res.t_sala_max);
    shell_print(sh, "  T_sala_min:          %.2f degC", (double)res.t_sala_min);
    shell_print(sh, "  Spread (Tmax-Tmin):  %.2f degC", (double)res.delta_t_sala_spread);
    shell_print(sh, "  Bypass Gradient:     %.2f degC", (double)res.delta_t_bypass);

    shell_print(sh, "--- Cooling Capacities & Balance ---");
    shell_print(sh, "  Q_sensible:          %.2f kW", (double)res.q_sensible_total);
    shell_print(sh, "  Q_latente:           %.2f kW", (double)res.q_latente_total);
    shell_print(sh, "  Q_total:             %.2f kW", (double)res.q_total);
    shell_print(sh, "  Condensate Rate:     %.2f L/h", (double)res.m_dot_condensate_lph);
    shell_print(sh, "  SHF:                 %.3f", (double)res.shf);
    shell_print(sh, "  %% Latent:            %.1f %%", (double)res.pct_latent_heat);

    shell_print(sh, "--- Efficiency & Exergy ---");
    shell_print(sh, "  Active Elec Power:   %.2f kW", (double)res.active_electrical_power_kw);
    shell_print(sh, "  COP_real:            %.2f", (double)res.cop_real);
    shell_print(sh, "  COP_sensible:        %.2f", (double)res.cop_sensible);
    shell_print(sh, "  COP_Carnot:          %.2f", (double)res.cop_carnot);
    shell_print(sh, "  eta_II (Exergy):     %.1f %%", (double)res.eta_ii_pct);

    shell_print(sh, "--- Baseline OLS Status ---");
    shell_print(sh, "  Trained:             %s", res.ols_trained ? "YES" : "NO");
    shell_print(sh, "  Training Hours:      %u / 168 h", res.training_hours_accumulated);
    shell_print(sh, "  Condition Number:    %.2f", (double)base.ols_condition_number);

    return 0;
}

static int cmd_thermal_pairs(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    config_thermal_site_t site;
    int err = app_facade_thermal_get_site_config(&site);
    if (err != 0) {
        shell_error(sh, "Failed to get site configuration: %d", err);
        return err;
    }

    thermal_energy_capture_t cap;
    (void)app_facade_thermal_get_capture(&cap);

    thermal_energy_result_t res;
    (void)app_facade_thermal_get_result(&res);

    uint8_t count = site.configured_pairs_count;
    if (count > THERMAL_MAX_PAIRS) {
        count = THERMAL_MAX_PAIRS;
    }

    shell_print(sh, "=== Configured Thermal Pairs (%u active) ===", count);
    shell_print(sh, "+------+------+--------------------+--------------------+----------+----------+----------+----------+-----------+----------+");
    shell_print(sh, "| Pair | Unit | Supply Probe       | Return Probe       | Delta T  | Q_sens   | Q_latent | Filter %% | Days Rem. | Leak Prob|");
    shell_print(sh, "+------+------+--------------------+--------------------+----------+----------+----------+----------+-----------+----------+");

    for (uint8_t i = 0U; i < count; i++) {
        uint8_t sup_id = site.pair_mapping_table[i].supply_flat_id;
        uint8_t ret_id = site.pair_mapping_table[i].return_flat_id;
        uint8_t unit_id = site.pair_mapping_table[i].hvac_unit_id;
        float t_inj = cap.t_inj[i];
        float t_ret = cap.t_ret[i];
        float dt = res.delta_t_pair[i];
        float qs = res.q_sensible_pair[i];
        float ql = res.q_latente_pair[i];
        float filt_pct = res.pct_filter_restriction[i];
        uint16_t days = res.filter_days_remaining[i];
        float leak_pct = res.pct_refrigerant_leak[i];

        char sup_buf[24];
        char ret_buf[24];
        snprintf(sup_buf, sizeof(sup_buf), "#%u: %.1f C", sup_id, (double)t_inj);
        snprintf(ret_buf, sizeof(ret_buf), "#%u: %.1f C", ret_id, (double)t_ret);

        shell_print(sh, "|  %2u  |  %2u  | %-18s | %-18s | %6.2f C | %6.2f kW| %6.2f kW| %6.1f %% |   %5u   |  %5.1f %% |",
                    i + 1U, unit_id, sup_buf, ret_buf,
                    (double)dt, (double)qs, (double)ql,
                    (double)filt_pct, days, (double)leak_pct);
    }
    shell_print(sh, "+------+------+--------------------+--------------------+----------+----------+----------+----------+-----------+----------+");

    return 0;
}

static int cmd_thermal_compressors(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    thermal_energy_result_t res;
    int err = app_facade_thermal_get_result(&res);
    if (err != 0) {
        shell_error(sh, "Failed to get thermal result: %d", err);
        return err;
    }

    config_thermal_baseline_t base;
    (void)app_facade_thermal_get_baseline(&base);

    thermal_hourly_record_t hist;
    (void)app_facade_thermal_get_hourly_history(0, &hist);

    shell_print(sh, "=== Compressor Temporal FSM Status (Up to 8 Units) ===");
    shell_print(sh, "+------+--------------------+--------------------+---------------+-------------+-------------+");
    shell_print(sh, "| Unit | FSM State          | Elapsed Duration   | Delta P (kW)  | Start Count | Short Trips |");
    shell_print(sh, "+------+--------------------+--------------------+---------------+-------------+-------------+");

    for (uint8_t u = 0U; u < THERMAL_MAX_HVAC_UNITS; u++) {
        compressor_fsm_state_t st = res.comp_fsm_state[u];
        float dp = base.compressor_power_signatures[u];

        const char *elapsed_desc;
        switch (st) {
        case COMPRESSOR_STATE_OFF:
            elapsed_desc = "t_on = 0s";
            break;
        case COMPRESSOR_STATE_INRUSH_WAIT:
            elapsed_desc = "t_on < 15s";
            break;
        case COMPRESSOR_STATE_STEP_LOAD_CAPTURE:
            elapsed_desc = "t_on = 15s";
            break;
        case COMPRESSOR_STATE_THERMAL_WARMUP:
            elapsed_desc = "15s < t_on < 180s";
            break;
        case COMPRESSOR_STATE_STEADY_STATE:
            elapsed_desc = "t_on >= 180s";
            break;
        default:
            elapsed_desc = "UNKNOWN";
            break;
        }

        shell_print(sh, "|  %2u  | %-18s | %-18s | %10.2f kW | %11u | %11u |",
                    u + 1U,
                    compressor_fsm_state_str(st),
                    elapsed_desc,
                    (double)dp,
                    hist.comp_starts_count,
                    0U);
    }
    shell_print(sh, "+------+--------------------+--------------------+---------------+-------------+-------------+");

    return 0;
}

static int cmd_thermal_diagnostics(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    thermal_energy_result_t res;
    int err = app_facade_thermal_get_result(&res);
    if (err != 0) {
        shell_error(sh, "Failed to get thermal result: %d", err);
        return err;
    }

    bool alert_hotspot  = (res.diagnostic_alarm_mask & (1U << 0)) != 0U;
    bool alert_bypass   = (res.diagnostic_alarm_mask & (1U << 1)) != 0U;
    bool alert_moisture = (res.diagnostic_alarm_mask & (1U << 2)) != 0U;

    shell_print(sh, "=== Predictive Analytics & Diagnostics ===");
    shell_print(sh, "--- Active Diagnostic Alerts ---");
    shell_print(sh, "  Hotspot (> 3.5 degC):  [%s] (Spread = %.2f degC)",
                alert_hotspot ? "ALERT ACTIVE" : "OK", (double)res.delta_t_sala_spread);
    shell_print(sh, "  Bypass (> 3.0 degC):   [%s] (Gradient = %.2f degC)",
                alert_bypass ? "ALERT ACTIVE" : "OK", (double)res.delta_t_bypass);
    shell_print(sh, "  Moisture (> 30 %%):     [%s] (Latent Load = %.1f %%)",
                alert_moisture ? "ALERT ACTIVE" : "OK", (double)res.pct_latent_heat);

    shell_print(sh, "--- Predictive Modeling ---");
    shell_print(sh, "  Expected Q_total:      %.2f kW", (double)res.q_total_expected_kw);
    shell_print(sh, "  Expected COP:          %.2f", (double)res.cop_expected);
    shell_print(sh, "  Mechanical Degradation:%.1f %%", (double)res.pct_mechanical_degradation);

    shell_print(sh, "--- Weather & Redundancy Margins ---");
    shell_print(sh, "  Weather-Corrected Q:   %.2f kW", (double)res.q_nominal_weather_corrected);
    shell_print(sh, "  Safe Redundant Q_max:  %.2f kW", (double)res.q_max_safe);
    shell_print(sh, "  Free Cooling Margin:   %.2f kW", (double)res.kw_free_cooling_margin);

    return 0;
}

static int cmd_thermal_baseline(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    config_thermal_baseline_t base;
    int err = app_facade_thermal_get_baseline(&base);
    if (err != 0) {
        shell_error(sh, "Failed to get thermal baseline: %d", err);
        return err;
    }

    shell_print(sh, "=== Machine-Learned Thermal Baseline (EEPROM Domain 6) ===");
    shell_print(sh, "  Status:              %s", base.is_valid ? "VALID / TRAINED" : "UNTRAINED");
    shell_print(sh, "  Training Hours:      %u / 168 h", base.training_hours_accumulated);
    shell_print(sh, "  Timestamp (UNIX):    %u", base.training_timestamp_unix);
    shell_print(sh, "  Condition Number:    %.2f", (double)base.ols_condition_number);
    shell_print(sh, "  Baseline Exergy:     %.1f %%", (double)base.baseline_exergetic_efficiency);
    shell_print(sh, "--- OLS Regression Coefficients ---");
    shell_print(sh, "  beta_0 (Intercept):  %.6f", base.ols_beta_coefficients[0]);
    shell_print(sh, "  beta_1 (T_ret):      %.6f", base.ols_beta_coefficients[1]);
    shell_print(sh, "  beta_2 (T_ext):      %.6f", base.ols_beta_coefficients[2]);
    shell_print(sh, "  beta_3 (f_carga):    %.6f", base.ols_beta_coefficients[3]);

    return 0;
}

static int cmd_thermal_site(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    config_thermal_site_t site;
    int err = app_facade_thermal_get_site_config(&site);
    if (err != 0) {
        shell_error(sh, "Failed to get site configuration: %d", err);
        return err;
    }

    shell_print(sh, "=== Site Thermal & HVAC Topology (EEPROM Domain 5) ===");
    shell_print(sh, "  Schema Version:      %u", site.version);
    shell_print(sh, "  Site Altitude (Z):   %d m", (int)site.site_altitude_m);
    shell_print(sh, "  Pairs Count (N):     %u", site.configured_pairs_count);
    shell_print(sh, "  Room Probes (M):     %u", site.configured_room_probes);
    shell_print(sh, "  Outdoor Probes (L):  %u", site.configured_outdoor_probes);
    shell_print(sh, "  Req Redundancy (U):  %u units", site.required_redundancy_units);
    shell_print(sh, "  Design Outdoor Temp: %.1f degC", (double)site.design_outdoor_temp_c);
    shell_print(sh, "  Derating Coeff:      %.4f", (double)site.derating_coefficient);

    shell_print(sh, "--- HVAC Units Nominal Ratings ---");
    shell_print(sh, "+------+--------------------+--------------------+");
    shell_print(sh, "| Unit | Airflow V_nom(m3/h)| Capacity Q_nom(kW) |");
    shell_print(sh, "+------+--------------------+--------------------+");
    for (uint8_t u = 0U; u < THERMAL_MAX_HVAC_UNITS; u++) {
        shell_print(sh, "|  %2u  |      %10.1f    |        %8.2f    |",
                    u + 1U,
                    (double)site.nominal_airflow_m3h[u],
                    (double)site.nominal_capacity_kw[u]);
    }
    shell_print(sh, "+------+--------------------+--------------------+");

    return 0;
}

static int cmd_thermal_history(const struct shell *sh, size_t argc, char **argv)
{
    uint16_t hours = 6U;
    if (argc > 1) {
        char *endptr = NULL;
        unsigned long val = strtoul(argv[1], &endptr, 10);
        if (endptr == argv[1] || val == 0U) {
            shell_error(sh, "Invalid hours count: %s", argv[1]);
            return -EINVAL;
        }
        if (val > 24U) {
            shell_warn(sh, "Clamping requested %lu hours to maximum of 24", val);
            val = 24U;
        }
        hours = (uint16_t)val;
    }

    shell_print(sh, "=== Thermal Hourly History (Last %u Hours) ===", hours);
    shell_print(sh, "+-----+------------+------------+------------+-------+---------+---------+--------+----------+----------+");
    shell_print(sh, "| Ago | Timestamp  | Q_tot(kWh) | P_el(kWh)  |  COP  | T_sala  | T_ext   | eta_II | CompMask | FanMask  |");
    shell_print(sh, "+-----+------------+------------+------------+-------+---------+---------+--------+----------+----------+");

    for (uint16_t h = 0U; h < hours; h++) {
        thermal_hourly_record_t rec;
        int err = app_facade_thermal_get_hourly_history(h, &rec);
        if (err == -ENOENT) {
            if (h == 0U) {
                shell_print(sh, "No closed hourly records recorded yet.");
            }
            break;
        }
        if (err != 0) {
            shell_error(sh, "Failed to read history at offset %u: %d", h, err);
            break;
        }

        float q_total = rec.sensible_capacity_kw + rec.latent_capacity_kw;
        float cop = (float)rec.cop_real_x100 / 100.0f;
        float eta_ii = (float)rec.eta_ii_x100 / 100.0f;
        float t_sala = (float)rec.t_room_x10[0] / 10.0f;
        float t_ext  = (float)rec.t_outdoor_x10[0] / 10.0f;

        shell_print(sh, "| %3u | %10u | %10.2f | %10.2f | %5.2f | %6.1fC | %6.1fC | %5.1f%% |  0x%04X  |  0x%04X  |",
                    h,
                    rec.timestamp_unix,
                    (double)q_total,
                    (double)rec.active_power_kw,
                    (double)cop,
                    (double)t_sala,
                    (double)t_ext,
                    (double)eta_ii,
                    rec.comp_state_mask,
                    rec.fan_state_mask);
    }
    shell_print(sh, "+-----+------------+------------+------------+-------+---------+---------+--------+----------+----------+");

    return 0;
}

static int cmd_thermal_recalibrate(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = app_facade_thermal_trigger_recalibration();
    if (err != 0) {
        shell_error(sh, "Failed to trigger thermal recalibration: %d", err);
        return err;
    }

    shell_print(sh, "Thermal baseline OLS recalibration triggered successfully.");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_thermal,
    SHELL_CMD(status, NULL, "Thermodynamic status and psychrometrics", cmd_thermal_status),
    SHELL_CMD(pairs, NULL, "Thermal supply/return pair metrics and diagnostics", cmd_thermal_pairs),
    SHELL_CMD(compressors, NULL, "Compressor temporal FSM states and step loads", cmd_thermal_compressors),
    SHELL_CMD(diagnostics, NULL, "Predictive alerts, derating, and safe margins", cmd_thermal_diagnostics),
    SHELL_CMD(baseline, NULL, "Machine-learned baseline OLS coefficients and exergy", cmd_thermal_baseline),
    SHELL_CMD(site, NULL, "Site thermal and HVAC topology configuration", cmd_thermal_site),
    SHELL_CMD_ARG(history, NULL, "Dump last N hourly datalogger records: thermal history [hours]", cmd_thermal_history, 1, 1),
    SHELL_CMD(recalibrate, NULL, "Trigger baseline OLS recalibration", cmd_thermal_recalibrate),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(thermal, &sub_thermal, "Thermal and energy thermodynamic monitoring commands", NULL);

/* =========================================================================
 * Real-Time Clock & System Time Shell Commands
 * ========================================================================= */

static const char *rtc_sync_state_str(rtc_sync_state_t state)
{
    switch (state) {
    case RTC_SYNC_STATE_UNINITIALIZED:    return "UNINITIALIZED";
    case RTC_SYNC_STATE_BATTERY_RETAINED: return "BATTERY_RETAINED";
    case RTC_SYNC_STATE_SYNCHRONIZED:     return "SYNCHRONIZED";
    case RTC_SYNC_STATE_DRIFTING:         return "DRIFTING";
    case RTC_SYNC_STATE_HARDWARE_FAULT:   return "HARDWARE_FAULT";
    default:                              return "UNKNOWN";
    }
}

static const char *rtc_time_source_str(rtc_time_source_t source)
{
    switch (source) {
    case RTC_TIME_SOURCE_NONE:          return "NONE";
    case RTC_TIME_SOURCE_BUILD_DEFAULT: return "BUILD_DEFAULT";
    case RTC_TIME_SOURCE_VBAT_BACKUP:   return "VBAT_BACKUP";
    case RTC_TIME_SOURCE_MANUAL_SHELL:  return "MANUAL_SHELL";
    case RTC_TIME_SOURCE_HMI_DWIN:      return "HMI_DWIN";
    case RTC_TIME_SOURCE_CLOUD_MQTT:    return "CLOUD_MQTT";
    case RTC_TIME_SOURCE_SNTP:          return "SNTP";
    default:                            return "UNKNOWN";
    }
}

static int cmd_rtc_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    rtc_sync_status_t status;
    int err = app_facade_time_get_status(&status);
    if (err != 0) {
        shell_error(sh, "Failed to query RTC status: %d", err);
        return err;
    }

    rtc_date_time_t dt;
    bool has_dt = (app_facade_time_get_datetime(&dt) == 0);

    shell_print(sh, "=================== RTC & SYSTEM TIME STATUS ===================");
    shell_print(sh, "Sync State       : %s", rtc_sync_state_str(status.sync_state));
    shell_print(sh, "Last Sync Source : %s", rtc_time_source_str(status.last_sync_source));
    shell_print(sh, "Last Sync Epoch  : %lld s", (long long)status.last_sync_epoch_s);
    shell_print(sh, "Last Sync Uptime : %lld ms", (long long)status.last_sync_uptime_ms);
    shell_print(sh, "Estimated Drift  : %d ppb", (int)status.estimated_drift_ppb);
    shell_print(sh, "Sync Cycle Count : %u", (unsigned int)status.sync_count);
    shell_print(sh, "Hardware Present : %s", status.sync_state != RTC_SYNC_STATE_HARDWARE_FAULT ? "YES" : "NO / FAULT");
    shell_print(sh, "Oscillator State : %s", status.oscillator_running ? "RUNNING & LOCKED" : "STOPPED / FAULT");
    shell_print(sh, "VBAT Power Domain: %s", status.battery_ok ? "OK (Retained)" : "DEAD / LOW");
    if (has_dt) {
        bool is_leap = ((dt.year % 4 == 0 && dt.year % 100 != 0) || (dt.year % 400 == 0));
        shell_print(sh, "Current Year     : %u (%s)", dt.year, is_leap ? "Leap Year" : "Common Year");
    }
    shell_print(sh, "================================================================");
    return 0;
}

static int cmd_rtc_get(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    rtc_date_time_t dt;
    int err = app_facade_time_get_datetime(&dt);
    if (err != 0) {
        shell_error(sh, "Failed to read RTC time: %d", err);
        return err;
    }

    char iso_buf[RTC_ISO8601_STR_LEN];
    if (app_facade_time_get_iso8601(iso_buf, sizeof(iso_buf)) != 0) {
        snprintf(iso_buf, sizeof(iso_buf), "ERROR");
    }

    int64_t epoch_s = 0;
    uint16_t sub_ms = 0;
    (void)app_facade_time_get_unix(&epoch_s, &sub_ms);

    rtc_sync_status_t status;
    (void)app_facade_time_get_status(&status);

    shell_print(sh, "Current UTC Time : %04u-%02u-%02u %02u:%02u:%02u.%03u UTC",
                dt.year, dt.month, dt.day, dt.hours, dt.minutes, dt.seconds, dt.milliseconds);
    shell_print(sh, "ISO-8601 String  : %s", iso_buf);
    shell_print(sh, "Unix Epoch       : %lld s (%lld ms)",
                (long long)epoch_s, (long long)epoch_s * 1000LL + sub_ms);
    shell_print(sh, "Time Source      : %s", rtc_time_source_str(status.last_sync_source));
    shell_print(sh, "Sync State       : %s", rtc_sync_state_str(status.sync_state));
    return 0;
}

static int cmd_rtc_set(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: rtc set <YYYY-MM-DD> <HH:MM:SS>");
        return -EINVAL;
    }

    unsigned int yr = 0, mo = 0, dy = 0;
    unsigned int hr = 0, mn = 0, sc = 0;

    if (sscanf(argv[1], "%u-%u-%u", &yr, &mo, &dy) != 3 ||
        sscanf(argv[2], "%u:%u:%u", &hr, &mn, &sc) != 3) {
        shell_error(sh, "Invalid date/time syntax. Expected <YYYY-MM-DD> <HH:MM:SS>");
        return -EINVAL;
    }

    rtc_date_time_t dt;
    memset(&dt, 0, sizeof(dt));
    dt.year = (uint16_t)yr;
    dt.month = (uint8_t)mo;
    dt.day = (uint8_t)dy;
    dt.hours = (uint8_t)hr;
    dt.minutes = (uint8_t)mn;
    dt.seconds = (uint8_t)sc;
    dt.milliseconds = 0;
    dt.day_of_week = 0;

    int err = app_facade_time_set_datetime(&dt, RTC_TIME_SOURCE_MANUAL_SHELL);
    if (err != 0) {
        shell_error(sh, "Failed to set RTC date/time: %d", err);
        return err;
    }

    shell_print(sh, "RTC calendar updated successfully: %04u-%02u-%02u %02u:%02u:%02u UTC",
                dt.year, dt.month, dt.day, dt.hours, dt.minutes, dt.seconds);
    return 0;
}

static int cmd_rtc_set_epoch(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: rtc set_epoch <seconds>");
        return -EINVAL;
    }

    char *endptr = NULL;
    int64_t epoch_s = (int64_t)strtoll(argv[1], &endptr, 10);
    if (endptr == argv[1] || epoch_s < 0) {
        shell_error(sh, "Invalid epoch timestamp: %s", argv[1]);
        return -EINVAL;
    }

    int err = app_facade_time_set_unix(epoch_s, RTC_TIME_SOURCE_MANUAL_SHELL);
    if (err != 0) {
        shell_error(sh, "Failed to set RTC epoch: %d", err);
        return err;
    }

    rtc_date_time_t dt;
    if (app_facade_time_get_datetime(&dt) == 0) {
        shell_print(sh, "RTC epoch updated successfully: %lld s (%04u-%02u-%02u %02u:%02u:%02u UTC)",
                    (long long)epoch_s, dt.year, dt.month, dt.day, dt.hours, dt.minutes, dt.seconds);
    } else {
        shell_print(sh, "RTC epoch updated successfully: %lld s", (long long)epoch_s);
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_rtc,
    SHELL_CMD(status, NULL, "Display synchronization source, drift, leap year, hardware presence", cmd_rtc_status),
    SHELL_CMD(get, NULL, "Print ISO-8601 formatted date/time and epoch seconds", cmd_rtc_get),
    SHELL_CMD_ARG(set, NULL, "Set calendar time manually: rtc set <YYYY-MM-DD> <HH:MM:SS>", cmd_rtc_set, 3, 0),
    SHELL_CMD_ARG(set_epoch, NULL, "Set time directly from UNIX epoch timestamp: rtc set_epoch <seconds>", cmd_rtc_set_epoch, 2, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(rtc, &sub_rtc, "RTC and System Time subsystem commands", NULL);

/* =========================================================================
 * MicroSD Storage Shell Commands
 * ========================================================================= */

static int cmd_sd_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    storage_fs_card_status_t status = STORAGE_FS_CARD_UNKNOWN;
    storage_fs_volume_info_t vol = {0};

    int err = app_facade_sd_get_status(&status, &vol);
    if (err != 0) {
        shell_error(sh, "Failed to retrieve SD card status: %d", err);
        return err;
    }

    const char *status_str = "UNKNOWN";
    switch (status) {
    case STORAGE_FS_CARD_NOT_INSERTED:
        status_str = "NOT_INSERTED (Card absent, PG5 HIGH)";
        break;
    case STORAGE_FS_CARD_INSERTED_UNMOUNTED:
        status_str = "INSERTED_UNMOUNTED (Card present, PG5 LOW, unmounted)";
        break;
    case STORAGE_FS_CARD_MOUNTED:
        status_str = "MOUNTED (Operational, ready for I/O)";
        break;
    case STORAGE_FS_CARD_EJECT_REQUESTED:
        status_str = "EJECT_REQUESTED (Draining operations / unmounting)";
        break;
    case STORAGE_FS_CARD_ERROR:
        status_str = "ERROR (Mount or hardware fault)";
        break;
    default:
        break;
    }

    shell_print(sh, "=== MicroSD Storage & Filesystem Status ===");
    shell_print(sh, "  Card Detect (PG5):   %s",
                (status != STORAGE_FS_CARD_NOT_INSERTED && status != STORAGE_FS_CARD_UNKNOWN) ? "DETECTED (LOW)" : "ABSENT (HIGH)");
    shell_print(sh, "  Lifecycle Status:    %s", status_str);
    shell_print(sh, "  Mount Point:         /SD:");

    if (status == STORAGE_FS_CARD_MOUNTED) {
        shell_print(sh, "  Filesystem Format:   %s", vol.fs_type[0] ? vol.fs_type : "FATFS");
        shell_print(sh, "  Volume Label:        %s", vol.volume_label[0] ? vol.volume_label : "N/A");
        shell_print(sh, "  Total Capacity:      %llu MiB", (unsigned long long)(vol.total_bytes / (1024ULL * 1024ULL)));
        shell_print(sh, "  Available Free Space:%llu MiB", (unsigned long long)(vol.free_bytes / (1024ULL * 1024ULL)));
        shell_print(sh, "  Sector / Cluster:    %u B / %u B", vol.sector_size_bytes, vol.cluster_size_bytes);
    }
    return 0;
}

static int cmd_sd_mount(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = app_facade_sd_mount();
    if (err != 0) {
        shell_error(sh, "Failed to request SD mount: %d", err);
        return err;
    }

    shell_print(sh, "MicroSD mount request submitted.");
    return 0;
}

static int cmd_sd_eject(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = app_facade_sd_eject();
    if (err != 0) {
        shell_error(sh, "Failed to request SD eject: %d", err);
        return err;
    }

    shell_print(sh, "MicroSD eject request submitted. Safe to remove when unmounted.");
    return 0;
}

static int cmd_sd_ls(const struct shell *sh, size_t argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/SD:";

    const i_storage_fs_t *fs = app_facade_get_sd_storage();
    if (fs == NULL || !i_storage_fs_is_bound(fs)) {
        shell_error(sh, "MicroSD filesystem port unavailable or uninitialized");
        return -ENODEV;
    }

    void *dir = NULL;
    int err = i_storage_fs_opendir(fs, &dir, path);
    if (err != 0) {
        shell_error(sh, "Failed to open directory %s: %d", path, err);
        return err;
    }

    shell_print(sh, "Directory listing of %s:", path);
    storage_fs_dirent_t entry;
    while (i_storage_fs_readdir(fs, dir, &entry) == 0) {
        if (entry.name[0] == '\0') {
            break;
        }
        if (entry.type == STORAGE_FS_ENTRY_DIR) {
            shell_print(sh, "  <DIR>         %s", entry.name);
        } else {
            shell_print(sh, "  %10llu B  %s", (unsigned long long)entry.size_bytes, entry.name);
        }
    }

    (void)i_storage_fs_closedir(fs, dir);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sd,
    SHELL_CMD(status, NULL, "Display card detect status (PG5), mount state, total and free space", cmd_sd_status),
    SHELL_CMD(mount, NULL, "Request manual mount of MicroSD card", cmd_sd_mount),
    SHELL_CMD(eject, NULL, "Request clean unmount / safe removal of MicroSD card", cmd_sd_eject),
    SHELL_CMD_ARG(ls, NULL, "List files and directories in path: sd ls [path]", cmd_sd_ls, 1, 1),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sd, &sub_sd, "MicroSD storage and filesystem commands", NULL);


