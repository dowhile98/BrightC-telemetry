/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the Modbus data contracts and their pure validators.
 */

#include <zephyr/ztest.h>

#include "common/modbus_types.h"

ZTEST_SUITE(modbus_types_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(modbus_types_suite, test_bus_max_matches_board_wiring)
{
    /* RS485-1 and RS485-2 only; array sizes in the container rely on this. */
    zassert_equal((int) MODBUS_BUS_MAX, 2, "BrightC V3 exposes exactly two RS-485 buses");
}

ZTEST(modbus_types_suite, test_bus_cfg_accepts_valid_combinations)
{
    const modbus_bus_cfg_t cfg_9600_8n1 = {
        .baudrate = 9600U,
        .parity = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    };
    const modbus_bus_cfg_t cfg_19200_8e1 = {
        .baudrate = 19200U,
        .parity = MODBUS_PARITY_EVEN,
        .stop_bits = 1U,
        .data_bits = 8U,
    };
    const modbus_bus_cfg_t cfg_115200_8o2 = {
        .baudrate = 115200U,
        .parity = MODBUS_PARITY_ODD,
        .stop_bits = 2U,
        .data_bits = 8U,
    };

    zassert_true(modbus_bus_cfg_is_valid(&cfg_9600_8n1), "9600 8N1 must be valid");
    zassert_true(modbus_bus_cfg_is_valid(&cfg_19200_8e1), "19200 8E1 must be valid");
    zassert_true(modbus_bus_cfg_is_valid(&cfg_115200_8o2), "115200 8O2 must be valid");
}

ZTEST(modbus_types_suite, test_bus_cfg_rejects_out_of_range_values)
{
    modbus_bus_cfg_t cfg = {
        .baudrate = 9600U,
        .parity = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    };

    zassert_false(modbus_bus_cfg_is_valid(NULL), "NULL must be rejected");

    cfg.baudrate = 0U;
    zassert_false(modbus_bus_cfg_is_valid(&cfg), "0 baud must be rejected");

    cfg.baudrate = MODBUS_BAUDRATE_MAX + 1U;
    zassert_false(modbus_bus_cfg_is_valid(&cfg), "Above maximum baud must be rejected");

    cfg.baudrate = 9600U;
    cfg.stop_bits = 3U;
    zassert_false(modbus_bus_cfg_is_valid(&cfg), "3 stop bits must be rejected");

    cfg.stop_bits = 1U;
    cfg.data_bits = 7U;
    zassert_false(modbus_bus_cfg_is_valid(&cfg), "7 data bits must be rejected");
}

ZTEST(modbus_types_suite, test_descriptor_accepts_valid_device)
{
    const modbus_slave_descriptor_t desc = {
        .device_id = 1U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 1U,
        .dev_type = MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    };

    zassert_true(modbus_slave_descriptor_is_valid(&desc), "Valid descriptor rejected");
}

ZTEST(modbus_types_suite, test_descriptor_rejects_invalid_fields)
{
    modbus_slave_descriptor_t desc = {
        .device_id = 1U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 1U,
        .dev_type = MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    };

    zassert_false(modbus_slave_descriptor_is_valid(NULL), "NULL descriptor must be rejected");

    desc.device_id = 0U;
    zassert_false(modbus_slave_descriptor_is_valid(&desc), "device_id 0 must be rejected");

    desc.device_id = 1U;
    desc.bus_id = (uint8_t) MODBUS_BUS_MAX;
    zassert_false(modbus_slave_descriptor_is_valid(&desc), "Unknown bus must be rejected");

    desc.bus_id = MODBUS_BUS_RS485_1;
    desc.slave_addr = 0U;
    zassert_false(modbus_slave_descriptor_is_valid(&desc), "Broadcast address must be rejected");

    desc.slave_addr = MODBUS_SLAVE_ADDR_MAX + 1U;
    zassert_false(modbus_slave_descriptor_is_valid(&desc), "Address 248 must be rejected");

    desc.slave_addr = MODBUS_SLAVE_ADDR_MAX;
    zassert_true(modbus_slave_descriptor_is_valid(&desc), "Address 247 must be accepted");

    desc.timeout_ms = 0U;
    zassert_false(modbus_slave_descriptor_is_valid(&desc), "Zero timeout must be rejected");

    desc.timeout_ms = 300U;
    desc.poll_interval_ms = 0U;
    zassert_false(modbus_slave_descriptor_is_valid(&desc), "Zero poll interval must be rejected");
}

ZTEST(modbus_types_suite, test_result_enum_distinguishes_failure_modes)
{
    /* A device that answers with an exception is alive: the registry must not
     * be able to confuse it with a silent device. */
    zassert_not_equal(MODBUS_RESULT_EXCEPTION, MODBUS_RESULT_TIMEOUT,
                      "Exception and timeout must remain distinct outcomes");
    zassert_not_equal(MODBUS_RESULT_CRC, MODBUS_RESULT_TIMEOUT,
                      "CRC and timeout must remain distinct outcomes");
}
