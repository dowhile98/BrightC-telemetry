/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file shell_snmp.c
 * @brief Zephyr Shell presentation interface for SNMP Agent configuration & diagnostics.
 *
 * Strictly adheres to Clean Architecture: interacts ONLY with app_facade.h.
 */

#include "presentation/shell/shell_snmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "application/services/app_facade.h"

static int cmd_snmp_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    config_snmp_t cfg;
    int err = app_facade_snmp_get_config(&cfg);
    if (err != 0) {
        shell_error(sh, "Failed to retrieve SNMP configuration: %d", err);
        return err;
    }

    shell_print(sh, "=== SNMP Agent Operational Status ===");
    shell_print(sh, "  Master Status:       %s", cfg.enabled ? "ENABLED" : "DISABLED");
    shell_print(sh, "  Listening Port:      UDP %u", cfg.listen_port);
    shell_print(sh, "  Versions Active:     %s%s",
                (cfg.version_mask & CONFIG_SNMP_VERSION_MASK_V1) ? "v1 " : "",
                (cfg.version_mask & CONFIG_SNMP_VERSION_MASK_V2C) ? "v2c" : "");
    shell_print(sh, "  Read Community:      %s", cfg.read_community);
    shell_print(sh, "  Write Community:     %s", cfg.write_community);
    shell_print(sh, "  System Name:         %s", cfg.sys_name);
    shell_print(sh, "  System Contact:      %s", cfg.sys_contact);
    shell_print(sh, "  System Location:     %s", cfg.sys_location);
    shell_print(sh, "  Engine Boots:        %u", cfg.engine_boots);
    shell_print(sh, "--- Trap Destinations (0..%u) ---", CONFIG_SNMP_MAX_TRAP_DESTS - 1);
    for (uint8_t i = 0; i < CONFIG_SNMP_MAX_TRAP_DESTS; i++) {
        shell_print(sh, "  [%u] %s:%u  (%s)",
                    i,
                    cfg.trap_dests[i].target_ip[0] ? cfg.trap_dests[i].target_ip : "<none>",
                    cfg.trap_dests[i].port,
                    cfg.trap_dests[i].enabled ? "ENABLED" : "DISABLED");
    }

    return 0;
}

static int cmd_snmp_enable(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: snmp enable <0|1>");
        return -EINVAL;
    }

    uint8_t en = (uint8_t)strtoul(argv[1], NULL, 0);
    int err = app_facade_snmp_set_enabled(en);
    if (err != 0) {
        shell_error(sh, "Failed to set SNMP enable state: %d", err);
        return err;
    }

    shell_print(sh, "SNMP agent %s.", en ? "enabled" : "disabled");
    return 0;
}

static int cmd_snmp_port(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: snmp port <1..65535>");
        return -EINVAL;
    }

    unsigned long port = strtoul(argv[1], NULL, 0);
    if (port == 0 || port > 65535) {
        shell_error(sh, "Invalid port number: %s", argv[1]);
        return -EINVAL;
    }

    int err = app_facade_snmp_set_port((uint16_t)port);
    if (err != 0) {
        shell_error(sh, "Failed to set SNMP port: %d", err);
        return err;
    }

    shell_print(sh, "SNMP port set to UDP %lu.", port);
    return 0;
}

static int cmd_snmp_community(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: snmp community <read|write> <string>");
        return -EINVAL;
    }

    int err;
    if (strcmp(argv[1], "read") == 0) {
        err = app_facade_snmp_set_community(argv[2], NULL);
    } else if (strcmp(argv[1], "write") == 0) {
        err = app_facade_snmp_set_community(NULL, argv[2]);
    } else {
        shell_error(sh, "Target must be 'read' or 'write'");
        return -EINVAL;
    }

    if (err != 0) {
        shell_error(sh, "Failed to update community: %d", err);
        return err;
    }

    shell_print(sh, "SNMP %s community updated.", argv[1]);
    return 0;
}

static int cmd_snmp_identity(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: snmp identity <name|contact|location> <string>");
        return -EINVAL;
    }

    int err;
    if (strcmp(argv[1], "name") == 0) {
        err = app_facade_snmp_set_identity(argv[2], NULL, NULL);
    } else if (strcmp(argv[1], "contact") == 0) {
        err = app_facade_snmp_set_identity(NULL, argv[2], NULL);
    } else if (strcmp(argv[1], "location") == 0) {
        err = app_facade_snmp_set_identity(NULL, NULL, argv[2]);
    } else {
        shell_error(sh, "Property must be 'name', 'contact', or 'location'");
        return -EINVAL;
    }

    if (err != 0) {
        shell_error(sh, "Failed to update identity: %d", err);
        return err;
    }

    shell_print(sh, "SNMP sys%s updated.", argv[1]);
    return 0;
}

static int cmd_snmp_trap_dest(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: snmp trap dest <slot 0..%u> <ip> [port] [enable 0|1]",
                    CONFIG_SNMP_MAX_TRAP_DESTS - 1);
        return -EINVAL;
    }

    unsigned long slot = strtoul(argv[1], NULL, 0);
    if (slot >= CONFIG_SNMP_MAX_TRAP_DESTS) {
        shell_error(sh, "Slot must be between 0 and %u", CONFIG_SNMP_MAX_TRAP_DESTS - 1);
        return -EINVAL;
    }

    const char *target_ip = argv[2];
    uint16_t port = 162U;
    uint8_t enabled = 1U;

    if (argc >= 4) {
        port = (uint16_t)strtoul(argv[3], NULL, 0);
    }
    if (argc >= 5) {
        enabled = (uint8_t)strtoul(argv[4], NULL, 0);
    }

    int err = app_facade_snmp_set_trap_dest((uint8_t)slot, target_ip, port, enabled);
    if (err != 0) {
        shell_error(sh, "Failed to configure trap destination: %d", err);
        return err;
    }

    shell_print(sh, "Trap destination slot %lu updated: %s:%u (%s).",
                slot, target_ip, port, enabled ? "enabled" : "disabled");
    return 0;
}

static int cmd_snmp_trap_test(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = app_facade_snmp_send_test_trap();
    if (err != 0) {
        shell_error(sh, "Failed to send test trap: %d", err);
        return err;
    }

    shell_print(sh, "Test SNMP trap submitted to system workqueue.");
    return 0;
}

static int cmd_snmp_save(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = app_facade_snmp_save();
    if (err < 0) {
        shell_error(sh, "Failed to save SNMP configuration to EEPROM: %d", err);
        return err;
    }

    shell_print(sh, "SNMP configuration successfully persisted to EEPROM.");
    return 0;
}

static int cmd_snmp_reload(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = app_facade_snmp_reload();
    if (err != 0) {
        shell_error(sh, "Failed to reload SNMP configuration from EEPROM: %d", err);
        return err;
    }

    shell_print(sh, "SNMP configuration reloaded from EEPROM and applied.");
    return 0;
}

static int cmd_snmp_reset(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = app_facade_snmp_reset_defaults();
    if (err != 0) {
        shell_error(sh, "Failed to reset SNMP configuration: %d", err);
        return err;
    }

    shell_print(sh, "SNMP configuration reset to compiled defaults (marked dirty).");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_snmp_trap,
    SHELL_CMD_ARG(dest, NULL, "Configure trap dest: <slot 0..3> <ip> [port] [enable 0|1]", cmd_snmp_trap_dest, 3, 2),
    SHELL_CMD(test, NULL, "Send immediate test trap to active targets", cmd_snmp_trap_test),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_snmp,
    SHELL_CMD(status, NULL, "Display active SNMP agent configuration and destinations", cmd_snmp_status),
    SHELL_CMD_ARG(enable, NULL, "Enable or disable agent: <0|1>", cmd_snmp_enable, 2, 0),
    SHELL_CMD_ARG(port, NULL, "Set UDP listening port: <port>", cmd_snmp_port, 2, 0),
    SHELL_CMD_ARG(community, NULL, "Set community: <read|write> <string>", cmd_snmp_community, 3, 0),
    SHELL_CMD_ARG(identity, NULL, "Set identity: <name|contact|location> <string>", cmd_snmp_identity, 3, 0),
    SHELL_CMD(trap, &sub_snmp_trap, "Configure trap destinations and test traps", NULL),
    SHELL_CMD(save, NULL, "Commit in-memory SNMP configuration to EEPROM", cmd_snmp_save),
    SHELL_CMD(reload, NULL, "Reload SNMP configuration from EEPROM into RAM", cmd_snmp_reload),
    SHELL_CMD(reset, NULL, "Reset SNMP configuration to factory defaults in RAM", cmd_snmp_reset),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(snmp, &sub_snmp, "SNMP Agent configuration and management", NULL);
