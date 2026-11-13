/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Port contract: every operation is reached through a `static inline` wrapper
 * that validates the handle and the arguments before anything is dispatched.
 * A caller must never touch `->api` and an unbound or misused port must never
 * reach the implementation.
 */

#include <zephyr/ztest.h>

#include <errno.h>
#include <string.h>

#include "interfaces/i_modbus_client.h"
#include "interfaces/i_slave_registry.h"

/*============================================================================*
 * Spy registry: counts every dispatch that reaches the vtable.
 *============================================================================*/

typedef struct {
    uint32_t calls;
    uint8_t last_bus_id;
    uint8_t last_device_id;
    uint64_t last_now_ms;
    modbus_transaction_result_t last_result;
    uint32_t last_max;
} spy_registry_t;

static spy_registry_t s_spy;

static int spy_claim(void *impl, uint8_t bus_id, uint64_t now_ms, modbus_poll_target_t *out)
{
    spy_registry_t *spy = (spy_registry_t *) impl;

    spy->calls++;
    spy->last_bus_id = bus_id;
    spy->last_now_ms = now_ms;

    out->device_id = 7U;

    return 0;
}

static int spy_record(void *impl, uint8_t device_id, modbus_transaction_result_t result,
                      uint64_t now_ms)
{
    spy_registry_t *spy = (spy_registry_t *) impl;

    spy->calls++;
    spy->last_device_id = device_id;
    spy->last_result = result;
    spy->last_now_ms = now_ms;

    return 0;
}

static int spy_next_due_in(void *impl, uint8_t bus_id, uint64_t now_ms, uint32_t *out_ms)
{
    spy_registry_t *spy = (spy_registry_t *) impl;

    spy->calls++;
    spy->last_bus_id = bus_id;
    spy->last_now_ms = now_ms;
    *out_ms = 42U;

    return 0;
}

static int spy_get_bus_cfg(void *impl, uint8_t bus_id, modbus_bus_cfg_t *out)
{
    spy_registry_t *spy = (spy_registry_t *) impl;

    spy->calls++;
    spy->last_bus_id = bus_id;
    out->baudrate = 19200U;

    return 0;
}

static int spy_snapshot(void *impl, uint8_t bus_id, modbus_slave_snapshot_t *out, size_t max,
                        size_t *out_count)
{
    spy_registry_t *spy = (spy_registry_t *) impl;

    spy->calls++;
    spy->last_bus_id = bus_id;
    spy->last_max = (uint32_t) max;
    *out_count = 0U;
    (void) out;

    return 0;
}

static const i_slave_registry_api_t s_spy_registry_api = {
    .claim_next_due = spy_claim,
    .record = spy_record,
    .next_due_in = spy_next_due_in,
    .get_bus_cfg = spy_get_bus_cfg,
    .snapshot = spy_snapshot,
};

/*============================================================================*
 * Spy client: counts every dispatch that reaches the vtable.
 *============================================================================*/

typedef struct {
    uint32_t calls;
    uint8_t last_addr;
    uint16_t last_reg;
    uint16_t last_quantity;
    uint16_t last_value;
    int32_t last_timeout;
} spy_client_t;

static spy_client_t s_client_spy;

static modbus_transaction_result_t spy_read(void *impl, uint8_t slave_addr, uint16_t reg_addr,
                                            uint16_t quantity, uint16_t *out, int32_t timeout_ms)
{
    spy_client_t *spy = (spy_client_t *) impl;

    spy->calls++;
    spy->last_addr = slave_addr;
    spy->last_reg = reg_addr;
    spy->last_quantity = quantity;
    spy->last_timeout = timeout_ms;
    out[0] = 0xBEEFU;

    return MODBUS_RESULT_OK;
}

static modbus_transaction_result_t spy_write(void *impl, uint8_t slave_addr, uint16_t reg_addr,
                                             uint16_t value, int32_t timeout_ms)
{
    spy_client_t *spy = (spy_client_t *) impl;

    spy->calls++;
    spy->last_addr = slave_addr;
    spy->last_reg = reg_addr;
    spy->last_value = value;
    spy->last_timeout = timeout_ms;

    return MODBUS_RESULT_OK;
}

static int spy_get_cfg(void *impl, modbus_bus_cfg_t *out)
{
    ((spy_client_t *) impl)->calls++;
    out->baudrate = 9600U;

    return 0;
}

static const i_modbus_client_api_t s_spy_client_api = {
    .read_holding_registers = spy_read,
    .write_single_register = spy_write,
    .get_bus_cfg = spy_get_cfg,
};

/*============================================================================*
 * Fixtures
 *============================================================================*/

static i_slave_registry_t make_registry(void)
{
    (void) memset(&s_spy, 0, sizeof(s_spy));

    const i_slave_registry_t registry = {
        .api = &s_spy_registry_api,
        .impl = &s_spy,
    };

    return registry;
}

static i_modbus_client_t make_client(void)
{
    (void) memset(&s_client_spy, 0, sizeof(s_client_spy));

    const i_modbus_client_t client = {
        .api = &s_spy_client_api,
        .impl = &s_client_spy,
    };

    return client;
}

ZTEST_SUITE(interfaces_suite, NULL, NULL, NULL, NULL, NULL);

/*============================================================================*
 * Registry port
 *============================================================================*/

ZTEST(interfaces_suite, test_unbound_registry_never_dispatches)
{
    const i_slave_registry_t unbound = {0};
    modbus_poll_target_t target;    modbus_slave_snapshot_t snap[1];
    modbus_bus_cfg_t cfg;
    size_t count = 0U;
    uint32_t wait_ms = 0U;

    (void) memset(&s_spy, 0, sizeof(s_spy));

    zassert_false(i_slave_registry_is_bound(&unbound), "A zeroed handle is not bound");
    zassert_false(i_slave_registry_is_bound(NULL), "NULL is not bound");

    zassert_equal(i_slave_registry_claim_next_due(&unbound, MODBUS_BUS_RS485_1, 0U, &target),
                  -EINVAL, "claim");
    zassert_equal(i_slave_registry_record(&unbound, 1U, MODBUS_RESULT_OK, 0U), -EINVAL, "record");
    zassert_equal(i_slave_registry_next_due_in(&unbound, MODBUS_BUS_RS485_1, 0U, &wait_ms),
                  -EINVAL, "next_due_in");
    zassert_equal(i_slave_registry_get_bus_cfg(&unbound, MODBUS_BUS_RS485_1, &cfg), -EINVAL,
                  "get_bus_cfg");
    zassert_equal(i_slave_registry_snapshot(&unbound, MODBUS_BUS_RS485_1, snap, 1U, &count),
                  -EINVAL, "snapshot");
    zassert_equal(s_spy.calls, 0U, "An unbound port must not reach any implementation");
}

ZTEST(interfaces_suite, test_registry_wrappers_reject_bad_arguments)
{
    i_slave_registry_t registry = make_registry();

    modbus_poll_target_t target;
    modbus_slave_snapshot_t snap[2];
    modbus_bus_cfg_t cfg;
    size_t count = 0U;
    uint32_t wait_ms = 0U;

    /* NULL handle on every operation. */
    zassert_equal(i_slave_registry_claim_next_due(NULL, MODBUS_BUS_RS485_1, 0U, &target), -EINVAL,
                  "NULL handle");
    zassert_equal(i_slave_registry_record(NULL, 1U, MODBUS_RESULT_OK, 0U), -EINVAL, "NULL handle");
    zassert_equal(i_slave_registry_next_due_in(NULL, MODBUS_BUS_RS485_1, 0U, &wait_ms), -EINVAL,
                  "NULL handle");
    zassert_equal(i_slave_registry_get_bus_cfg(NULL, MODBUS_BUS_RS485_1, &cfg), -EINVAL,
                  "NULL handle");
    zassert_equal(i_slave_registry_snapshot(NULL, MODBUS_BUS_RS485_1, snap, 2U, &count), -EINVAL,
                  "NULL handle");

    zassert_equal(s_spy.calls, 0U, "A NULL handle must not reach the implementation");

    zassert_equal(i_slave_registry_claim_next_due(&registry, MODBUS_BUS_RS485_1, 0U, NULL),
                  -EINVAL, "NULL target");
    zassert_equal(i_slave_registry_claim_next_due(&registry, (uint8_t) MODBUS_BUS_MAX, 0U, &target),
                  -EINVAL, "Bus id out of range");
    zassert_equal(i_slave_registry_claim_next_due(&registry, 0xFFU, 0U, &target), -EINVAL,
                  "Bus id out of range");

    zassert_equal(i_slave_registry_record(&registry, 0U, MODBUS_RESULT_OK, 0U), -EINVAL,
                  "Device id 0 does not exist");
    zassert_equal(i_slave_registry_record(&registry, 1U, (modbus_transaction_result_t) 99, 0U),
                  -EINVAL, "Unknown transaction result");

    zassert_equal(i_slave_registry_next_due_in(&registry, MODBUS_BUS_RS485_1, 0U, NULL), -EINVAL,
                  "NULL output");
    zassert_equal(i_slave_registry_next_due_in(&registry, (uint8_t) MODBUS_BUS_MAX, 0U, &wait_ms),
                  -EINVAL, "Bus id out of range");

    zassert_equal(i_slave_registry_get_bus_cfg(&registry, MODBUS_BUS_RS485_1, NULL), -EINVAL,
                  "NULL output");
    zassert_equal(i_slave_registry_get_bus_cfg(&registry, (uint8_t) MODBUS_BUS_MAX, &cfg), -EINVAL,
                  "Bus id out of range");

    zassert_equal(i_slave_registry_snapshot(&registry, MODBUS_BUS_RS485_1, NULL, 2U, &count),
                  -EINVAL, "NULL destination");
    zassert_equal(i_slave_registry_snapshot(&registry, MODBUS_BUS_RS485_1, snap, 2U, NULL),
                  -EINVAL, "NULL count");
    zassert_equal(i_slave_registry_snapshot(&registry, MODBUS_BUS_RS485_1, snap, 0U, &count),
                  -EINVAL, "Zero capacity");
    zassert_equal(i_slave_registry_snapshot(&registry, (uint8_t) MODBUS_BUS_MAX, snap, 2U, &count),
                  -EINVAL, "Bus id out of range");

    zassert_equal(s_spy.calls, 0U, "Rejected calls must never reach the implementation");
}

ZTEST(interfaces_suite, test_registry_wrappers_forward_valid_calls)
{
    i_slave_registry_t registry = make_registry();

    modbus_poll_target_t target;
    modbus_slave_snapshot_t snap[2];
    modbus_bus_cfg_t cfg;
    size_t count = 0U;
    uint32_t wait_ms = 0U;

    zassert_equal(i_slave_registry_claim_next_due(&registry, MODBUS_BUS_RS485_2, 1234U, &target), 0,
                  "claim");
    zassert_equal(s_spy.last_bus_id, (uint8_t) MODBUS_BUS_RS485_2, "Bus not forwarded");
    zassert_equal(s_spy.last_now_ms, 1234U, "Timestamp not forwarded");
    zassert_equal(target.device_id, 7U, "Output not forwarded");

    zassert_equal(i_slave_registry_record(&registry, 7U, MODBUS_RESULT_CRC, 2000U), 0, "record");
    zassert_equal(s_spy.last_result, MODBUS_RESULT_CRC, "Result not forwarded");
    zassert_equal(s_spy.last_now_ms, 2000U, "Timestamp not forwarded");

    zassert_equal(i_slave_registry_next_due_in(&registry, MODBUS_BUS_RS485_1, 3000U, &wait_ms), 0,
                  "next_due_in");
    zassert_equal(wait_ms, 42U, "Wait not forwarded");

    zassert_equal(i_slave_registry_get_bus_cfg(&registry, MODBUS_BUS_RS485_2, &cfg), 0,
                  "get_bus_cfg");
    zassert_equal(cfg.baudrate, 19200U, "Line configuration not forwarded");

    zassert_equal(i_slave_registry_snapshot(&registry, MODBUS_BUS_RS485_1, snap, 2U, &count), 0,
                  "snapshot");
    zassert_equal(s_spy.last_max, 2U, "Capacity not forwarded");

    zassert_equal(s_spy.calls, 5U, "Every valid call must be dispatched exactly once");
}

/*============================================================================*
 * Client port
 *============================================================================*/

ZTEST(interfaces_suite, test_unbound_client_never_dispatches)
{
    const i_modbus_client_t unbound = {0};
    uint16_t regs[2];
    modbus_bus_cfg_t cfg = {.baudrate = 9600U, .parity = MODBUS_PARITY_NONE, .stop_bits = 1U,
                            .data_bits = 8U};

    (void) memset(&s_client_spy, 0, sizeof(s_client_spy));

    zassert_false(i_modbus_client_is_bound(&unbound), "A zeroed handle is not bound");

    zassert_equal(i_modbus_client_read_holding_registers(&unbound, 1U, 0U, 2U, regs, 300),
                  MODBUS_RESULT_INVALID, "read");
    zassert_equal(i_modbus_client_write_single_register(&unbound, 1U, 0U, 1U, 300),
                  MODBUS_RESULT_INVALID, "write");
    zassert_equal(i_modbus_client_get_bus_cfg(&unbound, &cfg), -EINVAL, "get_bus_cfg");
    zassert_equal(s_client_spy.calls, 0U, "An unbound port must not reach any implementation");
}

ZTEST(interfaces_suite, test_client_wrappers_reject_bad_arguments)
{
    i_modbus_client_t client = make_client();

    uint16_t regs[2];

    zassert_equal(i_modbus_client_read_holding_registers(&client, 1U, 0U, 2U, NULL, 300),
                  MODBUS_RESULT_INVALID, "NULL destination");
    zassert_equal(i_modbus_client_read_holding_registers(&client, 0U, 0U, 2U, regs, 300),
                  MODBUS_RESULT_INVALID, "Broadcast address is not readable");
    zassert_equal(i_modbus_client_read_holding_registers(&client, 248U, 0U, 2U, regs, 300),
                  MODBUS_RESULT_INVALID, "Address out of range");
    zassert_equal(i_modbus_client_read_holding_registers(&client, 1U, 0U, 0U, regs, 300),
                  MODBUS_RESULT_INVALID, "Zero registers");
    zassert_equal(i_modbus_client_read_holding_registers(
                      &client, 1U, 0U, (uint16_t) (MODBUS_READ_HOLDING_MAX_REGISTERS + 1U), regs,
                      300),
                  MODBUS_RESULT_INVALID, "More registers than FC03 allows");
    zassert_equal(i_modbus_client_read_holding_registers(&client, 1U, 0xFFFFU, 2U, regs, 300),
                  MODBUS_RESULT_INVALID, "The block would run past register 0xFFFF");
    zassert_equal(i_modbus_client_read_holding_registers(&client, 1U, 0U, 2U, regs, 0),
                  MODBUS_RESULT_INVALID, "A non-positive timeout can never succeed");

    zassert_equal(i_modbus_client_write_single_register(&client, 1U, 0U, 1U, -5),
                  MODBUS_RESULT_INVALID, "A non-positive timeout can never succeed");
    zassert_equal(i_modbus_client_write_single_register(&client, 1U, 0U, 1U, 0),
                  MODBUS_RESULT_INVALID, "A non-positive timeout can never succeed");
    zassert_equal(i_modbus_client_write_single_register(&client, 1U, 0U, 1U, 300), 0, "valid");
    zassert_equal(s_client_spy.calls, 1U, "Only the valid write may be dispatched");

    (void) memset(&s_client_spy, 0, sizeof(s_client_spy));

    zassert_equal(i_modbus_client_get_bus_cfg(&client, NULL), -EINVAL, "NULL output");
    zassert_equal(s_client_spy.calls, 0U, "A NULL output must not reach the implementation");
}

ZTEST(interfaces_suite, test_client_wrappers_forward_valid_calls)
{
    i_modbus_client_t client = make_client();

    uint16_t regs[2];
    modbus_bus_cfg_t readback;

    zassert_equal(i_modbus_client_read_holding_registers(&client, 1U, 0x0020U, 2U, regs, 300),
                  MODBUS_RESULT_OK, "read");
    zassert_equal(s_client_spy.last_addr, 1U, "Address not forwarded");
    zassert_equal(s_client_spy.last_quantity, 2U, "Quantity not forwarded");
    zassert_equal(s_client_spy.last_timeout, 300, "Timeout not forwarded");
    zassert_equal(regs[0], 0xBEEFU, "Destination not forwarded");

    zassert_equal(i_modbus_client_write_single_register(&client, 2U, 0x0100U, 0x0102U, 250),
                  MODBUS_RESULT_OK, "write");
    zassert_equal(s_client_spy.last_value, 0x0102U, "Value not forwarded");

    zassert_equal(i_modbus_client_get_bus_cfg(&client, &readback), 0, "get_bus_cfg");
    zassert_equal(readback.baudrate, 9600U, "Line configuration not forwarded");

    zassert_equal(s_client_spy.calls, 3U, "Every valid call must be dispatched exactly once");
}
