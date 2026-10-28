/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * TDD Unit Tests for CWT-TM-16PT Temperature Driver and Modbus Slave Manager.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include "cwt_tm16pt.h"
#include "modbus_slave_manager.h"
#include "common/modbus_slave_config.h"

/* =========================================================================
 * 1. CWT-TM-16PT Sensor Driver Tests
 * ========================================================================= */
ZTEST_SUITE(cwt_tm16pt_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(cwt_tm16pt_suite, test_cwt_tm16pt_parse_scaled_normal)
{
    uint16_t simulated_regs[16];
    /* Channel 0: 254 = 25.4 C
     * Channel 1: 300 = 30.0 C
     * Channel 2: -150 = -15.0 C (int16 0xFF6A)
     * Channels 3-15: 200 = 20.0 C */
    simulated_regs[0] = 254;
    simulated_regs[1] = 300;
    simulated_regs[2] = (uint16_t)(-150);
    for (int i = 3; i < 16; i++) {
        simulated_regs[i] = 200;
    }

    temperature_bank_16ch_t bank;
    memset(&bank, 0, sizeof(bank));

    int err = cwt_tm16pt_parse_scaled_temperatures(simulated_regs, 16, &bank);
    zassert_equal(err, 0, "Parser returned error");
    zassert_true(bank.is_communication_valid, "Communication should be valid");
    zassert_equal(bank.open_probe_mask, 0, "No open probes expected");
    zassert_equal(bank.valid_channels_mask, 0xFFFF, "All 16 channels must be valid");

    zassert_within(bank.temperatures_c[0], 25.4f, 0.01f, "Ch0 mismatch");
    zassert_within(bank.temperatures_c[1], 30.0f, 0.01f, "Ch1 mismatch");
    zassert_within(bank.temperatures_c[2], -15.0f, 0.01f, "Ch2 negative temp mismatch");
    zassert_within(bank.temperatures_c[15], 20.0f, 0.01f, "Ch15 mismatch");
}

ZTEST(cwt_tm16pt_suite, test_cwt_tm16pt_parse_scaled_open_probe_detection)
{
    uint16_t simulated_regs[16];
    for (int i = 0; i < 16; i++) {
        simulated_regs[i] = 220; /* 22.0 C */
    }

    /* Channel 3: Disconnected probe (0x7FFF = 32767) */
    simulated_regs[3] = CWT_TM16PT_PROBE_OPEN_CODE;
    /* Channel 7: Broken probe sentinel (-32768) */
    simulated_regs[7] = 0x8000;

    temperature_bank_16ch_t bank;
    memset(&bank, 0, sizeof(bank));

    int err = cwt_tm16pt_parse_scaled_temperatures(simulated_regs, 16, &bank);
    zassert_equal(err, 0, "Parser returned error");

    /* Channel 3 and Channel 7 must be in open probe mask */
    uint16_t expected_open = (1U << 3) | (1U << 7);
    zassert_equal(bank.open_probe_mask, expected_open, "Open probe mask mismatch");

    /* Valid mask must NOT have bit 3 and 7 */
    zassert_false((bank.valid_channels_mask & (1U << 3)), "Ch3 must not be valid");
    zassert_false((bank.valid_channels_mask & (1U << 7)), "Ch7 must not be valid");
    zassert_true((bank.valid_channels_mask & (1U << 0)), "Ch0 must be valid");
}

ZTEST(cwt_tm16pt_suite, test_cwt_tm16pt_config_register_build_and_parse)
{
    uint16_t reg_val = 0;

    /* Build config: Slave ID 1, 9600 bps, None Parity (0) */
    int err = cwt_tm16pt_build_config_register(1, 9600, 0, &reg_val);
    zassert_equal(err, 0, "Build failed for default 9600 8N1");
    zassert_equal(reg_val, 0x0100, "Expected 0x0100 for ID 1, 9600, None");

    uint8_t out_id = 0, out_parity = 0;
    uint32_t out_baud = 0;
    err = cwt_tm16pt_parse_config_register(reg_val, &out_id, &out_baud, &out_parity);
    zassert_equal(err, 0, "Parse failed");
    zassert_equal(out_id, 1, "Slave ID mismatch");
    zassert_equal(out_baud, 9600, "Baudrate mismatch");
    zassert_equal(out_parity, 0, "Parity mismatch");

    /* Build config: Slave ID 10, 19200 bps (code 6), Even Parity (code 1) */
    /* low byte: 6 | (1 << 3) = 6 | 8 = 14 = 0x0E. High byte: 10 = 0x0A -> 0x0A0E */
    err = cwt_tm16pt_build_config_register(10, 19200, 1, &reg_val);
    zassert_equal(err, 0, "Build failed for 19200 Even");
    zassert_equal(reg_val, 0x0A0E, "Expected 0x0A0E for ID 10, 19200, Even");

    err = cwt_tm16pt_parse_config_register(reg_val, &out_id, &out_baud, &out_parity);
    zassert_equal(err, 0, "Parse failed");
    zassert_equal(out_id, 10, "Slave ID mismatch");
    zassert_equal(out_baud, 19200, "Baudrate mismatch");
    zassert_equal(out_parity, 1, "Parity mismatch");
}

/* =========================================================================
 * 2. Modbus Slave Manager & Failover Tests
 * ========================================================================= */
ZTEST_SUITE(modbus_slave_manager_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(modbus_slave_manager_suite, test_manager_init_and_failover_bus_change)
{
    /* 1. Init manager: registers Device ID 1 (CWT-TM-16PT) on RS485-1 */
    int err = modbus_slave_manager_init();
    zassert_equal(err, 0, "Init failed");

    modbus_slave_descriptor_t desc;
    err = modbus_slave_manager_get(1, &desc);
    zassert_equal(err, 0, "Device 1 not found");
    zassert_equal(desc.bus_id, MODBUS_BUS_RS485_1, "Default bus should be RS485-1");
    zassert_equal(desc.slave_addr, 1, "Default addr should be 1");
    zassert_equal(desc.dev_type, MODBUS_DEV_TYPE_CWT_TM16PT, "Dev type mismatch");

    /* Query bus 1 devices */
    uint8_t dev_ids[4];
    size_t count = 0;
    err = modbus_slave_manager_get_bus_devices(MODBUS_BUS_RS485_1, dev_ids, 4, &count);
    zassert_equal(err, 0, "Bus 1 query failed");
    zassert_equal(count, 1, "Expected 1 device on Bus 1");
    zassert_equal(dev_ids[0], 1, "Expected Device ID 1");

    /* 2. DYNAMIC FAILOVER: Transceiver 1 damaged! Move Device 1 to RS485-2 */
    err = modbus_slave_manager_set_bus(1, MODBUS_BUS_RS485_2);
    zassert_equal(err, 0, "Set bus failed");

    /* Verify device 1 is NO LONGER on Bus 1 */
    count = 0;
    modbus_slave_manager_get_bus_devices(MODBUS_BUS_RS485_1, dev_ids, 4, &count);
    zassert_equal(count, 0, "Bus 1 should have 0 devices now");

    /* Verify device 1 is NOW active on Bus 2 */
    count = 0;
    modbus_slave_manager_get_bus_devices(MODBUS_BUS_RS485_2, dev_ids, 4, &count);
    zassert_equal(count, 1, "Bus 2 should have 1 device");
    zassert_equal(dev_ids[0], 1, "Device 1 should be on Bus 2");
}

ZTEST(modbus_slave_manager_suite, test_manager_offline_backoff_state_machine)
{
    modbus_slave_manager_init();

    modbus_slave_runtime_state_t st;
    modbus_slave_manager_get_runtime_state(1, &st);
    zassert_equal(st.health, MODBUS_HEALTH_ONLINE, "Initial health should be ONLINE");

    /* Fail 1: should become DEGRADED */
    modbus_slave_manager_record_result(1, false, 1000);
    modbus_slave_manager_get_runtime_state(1, &st);
    zassert_equal(st.health, MODBUS_HEALTH_DEGRADED, "Health should be DEGRADED after 1 fail");

    /* Fail 2: still DEGRADED */
    modbus_slave_manager_record_result(1, false, 2000);
    modbus_slave_manager_get_runtime_state(1, &st);
    zassert_equal(st.health, MODBUS_HEALTH_DEGRADED, "Health should still be DEGRADED");

    /* Fail 3: triggers OFFLINE BACKOFF threshold! */
    modbus_slave_manager_record_result(1, false, 3000);
    modbus_slave_manager_get_runtime_state(1, &st);
    zassert_equal(st.health, MODBUS_HEALTH_OFFLINE_BACKOFF, "Health must transition to OFFLINE BACKOFF");

    /* Verify Backoff Polling behavior:
     * Last poll was at 3000 ms.
     * At 4500 ms (elapsed 1500 ms): normal 1000ms poll interval would trigger,
     * but because of backoff (10000ms threshold), it MUST NOT be ready! */
    bool ready = modbus_slave_manager_is_ready_to_poll(1, 4500);
    zassert_false(ready, "Device in offline backoff must NOT poll at normal rate!");

    /* At 13500 ms (elapsed 10500 ms >= 10000 ms): Probe ping should trigger */
    ready = modbus_slave_manager_is_ready_to_poll(1, 13500);
    zassert_true(ready, "Probe ping should trigger after backoff interval");

    /* Success: device responds again! Immediate recovery to ONLINE */
    modbus_slave_manager_record_result(1, true, 13550);
    modbus_slave_manager_get_runtime_state(1, &st);
    zassert_equal(st.health, MODBUS_HEALTH_ONLINE, "Health must recover to ONLINE on success");
    zassert_equal(st.consecutive_fails, 0, "Consecutive fails must reset to 0");
}

ZTEST(modbus_slave_manager_suite, test_manager_export_import_serialization)
{
    modbus_slave_manager_init();

    /* Register a second device */
    modbus_slave_descriptor_t dev2 = {
        .device_id = 2,
        .bus_id = MODBUS_BUS_RS485_2,
        .slave_addr = 5,
        .dev_type = MODBUS_DEV_TYPE_ENERGY_METER_SDM,
        .poll_interval_ms = 500,
        .timeout_ms = 250,
        .is_enabled = true,
    };
    modbus_slave_manager_register(&dev2);

    /* Export to raw buffer (as microconf / EEPROM will do) */
    uint8_t buffer[256];
    size_t exported_size = 0;
    int err = modbus_slave_manager_export_config(buffer, sizeof(buffer), &exported_size);
    zassert_equal(err, 0, "Export failed");
    zassert_true(exported_size > 0, "Exported size must be > 0");

    /* Reset / re-import */
    err = modbus_slave_manager_import_config(buffer, exported_size);
    zassert_equal(err, 0, "Import failed");

    /* Verify device 1 and device 2 intact */
    modbus_slave_descriptor_t d1, d2;
    zassert_equal(modbus_slave_manager_get(1, &d1), 0, "Device 1 missing after import");
    zassert_equal(d1.slave_addr, 1, "D1 slave addr mismatch");

    zassert_equal(modbus_slave_manager_get(2, &d2), 0, "Device 2 missing after import");
    zassert_equal(d2.slave_addr, 5, "D2 slave addr mismatch");
    zassert_equal(d2.dev_type, MODBUS_DEV_TYPE_ENERGY_METER_SDM, "D2 dev type mismatch");
    zassert_equal(d2.bus_id, MODBUS_BUS_RS485_2, "D2 bus mismatch");
}
