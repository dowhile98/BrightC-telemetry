/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the application-layer Modbus registry: table limits, atomic
 * claims, round-robin scheduling, bus failover, serialisation and concurrent
 * access from two polling threads.
 */

#include <zephyr/ztest.h>
#include <zephyr/sys/atomic.h>

#include <errno.h>
#include <string.h>

#include "application/modbus/modbus_registry.h"
#include "common/modbus_types.h"

static const modbus_bus_cfg_t k_bus_cfg[MODBUS_BUS_MAX] = {
    [MODBUS_BUS_RS485_1] = {
        .baudrate = 9600U,
        .parity = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    },
    [MODBUS_BUS_RS485_2] = {
        .baudrate = 19200U,
        .parity = MODBUS_PARITY_EVEN,
        .stop_bits = 1U,
        .data_bits = 8U,
    },
};

static modbus_slave_descriptor_t make_desc(uint8_t device_id, uint8_t bus_id, uint8_t addr)
{
    modbus_slave_descriptor_t desc = {
        .device_id = device_id,
        .bus_id = bus_id,
        .slave_addr = addr,
        .dev_type = MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    };

    return desc;
}

/* The registry is ~2 KB; keep it off the ztest stack. */
static modbus_registry_t s_registry;

static void reset_registry(void)
{
    zassert_equal(modbus_registry_init(&s_registry, k_bus_cfg), 0, "Registry init failed");
}

ZTEST_SUITE(modbus_registry_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(modbus_registry_suite, test_init_rejects_bad_arguments)
{
    zassert_equal(modbus_registry_init(&s_registry, NULL), -EINVAL, "NULL cfg must be rejected");

    modbus_bus_cfg_t broken[MODBUS_BUS_MAX] = {
        [MODBUS_BUS_RS485_1] = {.baudrate = 9600U, .parity = MODBUS_PARITY_NONE,
                                .stop_bits = 1U, .data_bits = 8U},
        [MODBUS_BUS_RS485_2] = {.baudrate = 100U, .parity = MODBUS_PARITY_NONE,
                                .stop_bits = 1U, .data_bits = 8U},
    };

    zassert_equal(modbus_registry_init(&s_registry, broken), -EINVAL,
                  "An invalid bus configuration must be rejected");
}

ZTEST(modbus_registry_suite, test_bus_cfg_roundtrip)
{
    reset_registry();

    for (uint8_t bus = 0U; bus < (uint8_t) MODBUS_BUS_MAX; bus++) {
        modbus_bus_cfg_t out;

        zassert_equal(modbus_registry_get_bus_cfg(&s_registry, bus, &out), 0, "Get bus cfg failed");
        zassert_equal(out.baudrate, k_bus_cfg[bus].baudrate, "Baudrate mismatch");
        zassert_equal(out.parity, k_bus_cfg[bus].parity, "Parity mismatch");
    }

    modbus_bus_cfg_t out;
    zassert_equal(modbus_registry_get_bus_cfg(&s_registry, (uint8_t) MODBUS_BUS_MAX, &out), -EINVAL,
                  "Out-of-range bus must be rejected");
}

ZTEST(modbus_registry_suite, test_register_and_snapshot)
{
    reset_registry();

    modbus_slave_descriptor_t desc = make_desc(1U, MODBUS_BUS_RS485_1, 1U);

    zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Register failed");
    zassert_equal(modbus_registry_register(&s_registry, NULL), -EINVAL, "NULL descriptor");

    modbus_slave_snapshot_t snap[4];
    size_t count = 0U;

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap, 4U, &count), 0,
                  "Snapshot failed");
    zassert_equal(count, 1U, "Expected one slave on bus 1");
    zassert_equal(snap[0].descriptor.device_id, 1U, "Device id mismatch");
    zassert_equal(snap[0].descriptor.slave_addr, 1U, "Slave address mismatch");
    zassert_equal(snap[0].runtime.health, MODBUS_HEALTH_ONLINE, "Fresh slave must be ONLINE");

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_2, snap, 4U, &count), 0,
                  "Snapshot failed");
    zassert_equal(count, 0U, "Bus 2 must be empty");

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, NULL, 4U, &count),
                  -EINVAL, "NULL output must be rejected");
    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap, 4U, NULL),
                  -EINVAL, "NULL count must be rejected");
}

ZTEST(modbus_registry_suite, test_register_rejects_duplicate_address_on_same_bus)
{
    reset_registry();

    modbus_slave_descriptor_t first = make_desc(1U, MODBUS_BUS_RS485_1, 7U);
    modbus_slave_descriptor_t clash = make_desc(2U, MODBUS_BUS_RS485_1, 7U);
    modbus_slave_descriptor_t other_bus = make_desc(3U, MODBUS_BUS_RS485_2, 7U);

    zassert_equal(modbus_registry_register(&s_registry, &first), 0, "First register failed");
    zassert_equal(modbus_registry_register(&s_registry, &clash), -EADDRINUSE,
                  "Duplicate address on the same bus must be rejected");
    zassert_equal(modbus_registry_register(&s_registry, &other_bus), 0,
                  "The same address on another bus is allowed");
}

ZTEST(modbus_registry_suite, test_register_same_device_updates_in_place)
{
    reset_registry();

    modbus_slave_descriptor_t desc = make_desc(1U, MODBUS_BUS_RS485_1, 1U);

    zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Register failed");

    desc.poll_interval_ms = 500U;
    zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Update failed");

    modbus_slave_snapshot_t snap[4];
    size_t count = 0U;

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap, 4U, &count), 0,
                  "Snapshot failed");
    zassert_equal(count, 1U, "Update must not append a second entry");
    zassert_equal(snap[0].descriptor.poll_interval_ms, 500U, "Interval not updated");
}

ZTEST(modbus_registry_suite, test_register_rejects_invalid_descriptor)
{
    reset_registry();

    modbus_slave_descriptor_t desc = make_desc(1U, MODBUS_BUS_RS485_1, 1U);

    desc.device_id = 0U;
    zassert_equal(modbus_registry_register(&s_registry, &desc), -EINVAL, "device_id 0");

    desc = make_desc(1U, MODBUS_BUS_RS485_1, 1U);
    desc.slave_addr = 0U;
    zassert_equal(modbus_registry_register(&s_registry, &desc), -EINVAL, "Broadcast address");

    desc = make_desc(1U, (uint8_t) MODBUS_BUS_MAX, 1U);
    zassert_equal(modbus_registry_register(&s_registry, &desc), -EINVAL, "Unknown bus");
}

ZTEST(modbus_registry_suite, test_per_bus_capacity_is_enforced)
{
    reset_registry();

    for (uint8_t i = 0U; i < MODBUS_MAX_SLAVES_PER_BUS; i++) {
        modbus_slave_descriptor_t desc = make_desc(i + 1U, MODBUS_BUS_RS485_1, i + 1U);

        zassert_equal(modbus_registry_register(&s_registry, &desc), 0,
                      "Register failed at slot %u", i + 1U);
    }

    modbus_slave_descriptor_t overflow =
        make_desc(MODBUS_MAX_SLAVES_PER_BUS + 1U, MODBUS_BUS_RS485_1,
                  MODBUS_MAX_SLAVES_PER_BUS + 1U);

    zassert_equal(modbus_registry_register(&s_registry, &overflow), -ENOSPC,
                  "Bus beyond its capacity must be rejected");
}

ZTEST(modbus_registry_suite, test_total_capacity_is_enforced)
{
    reset_registry();

    uint8_t id = 1U;
    uint8_t addr = 1U;
    uint8_t bus = MODBUS_BUS_RS485_1;

    for (size_t i = 0U; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        if (i == MODBUS_MAX_SLAVES_PER_BUS) {
            bus = MODBUS_BUS_RS485_2;
            addr = 1U;
        }

        modbus_slave_descriptor_t desc = make_desc(id, bus, addr);

        zassert_equal(modbus_registry_register(&s_registry, &desc), 0,
                      "Register failed at device %u", id);

        id++;
        addr++;
    }

    modbus_slave_descriptor_t overflow = make_desc(id, MODBUS_BUS_RS485_2, addr);

    zassert_equal(modbus_registry_register(&s_registry, &overflow), -ENOSPC,
                  "Table beyond its total capacity must be rejected");
}

ZTEST(modbus_registry_suite, test_claim_is_round_robin_and_not_due_returns_enoent)
{
    reset_registry();

    for (uint8_t i = 0U; i < 3U; i++) {
        modbus_slave_descriptor_t desc = make_desc(i + 1U, MODBUS_BUS_RS485_1, i + 1U);

        zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Register failed");
    }

    modbus_poll_target_t target;

    /* Every slave is due at boot: they must be claimed in registration order. */
    for (uint8_t expected = 1U; expected <= 3U; expected++) {
        zassert_equal(modbus_registry_claim_next_due(&s_registry, MODBUS_BUS_RS485_1, 0U, &target),
                      0, "Claim %u failed", expected);
        zassert_equal(target.device_id, expected, "Round-robin order violated");
        zassert_equal(target.slave_addr, expected, "Slave address mismatch");
        zassert_equal(target.timeout_ms, 300U, "Timeout not propagated");
        zassert_equal(modbus_registry_record(&s_registry, target.device_id, MODBUS_RESULT_OK, 0U),
                      0, "Record failed");
    }

    zassert_equal(modbus_registry_claim_next_due(&s_registry, MODBUS_BUS_RS485_1, 0U, &target),
                  -ENOENT, "Nothing is due right after a full sweep");

    zassert_equal(modbus_registry_claim_next_due(&s_registry, MODBUS_BUS_RS485_1,
                                                1000U, &target),
                  0, "Slaves must be due again after the interval");
    zassert_equal(target.device_id, 1U, "Cursor must wrap to the first slave");
}

ZTEST(modbus_registry_suite, test_claim_hides_in_flight_slave)
{
    reset_registry();

    for (uint8_t i = 0U; i < 2U; i++) {
        modbus_slave_descriptor_t desc = make_desc(i + 1U, MODBUS_BUS_RS485_1, i + 1U);

        zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Register failed");
    }

    modbus_poll_target_t first;
    modbus_poll_target_t second;

    zassert_equal(modbus_registry_claim_next_due(&s_registry, MODBUS_BUS_RS485_1, 0U, &first), 0,
                  "First claim failed");
    zassert_equal(modbus_registry_claim_next_due(&s_registry, MODBUS_BUS_RS485_1, 0U, &second), 0,
                  "Second claim failed");
    zassert_not_equal(first.device_id, second.device_id,
                      "A claimed slave must not be handed out twice");

    modbus_poll_target_t none;
    zassert_equal(modbus_registry_claim_next_due(&s_registry, MODBUS_BUS_RS485_1, 0U, &none),
                  -ENOENT, "Both slaves are in flight");
}

ZTEST(modbus_registry_suite, test_stale_claim_is_reclaimed)
{
    reset_registry();

    modbus_slave_descriptor_t desc = make_desc(1U, MODBUS_BUS_RS485_1, 1U);
    zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Register failed");

    modbus_poll_target_t target;
    zassert_equal(modbus_registry_claim_next_due(&s_registry, MODBUS_BUS_RS485_1, 1000U, &target),
                  0, "Claim failed");

    zassert_equal(modbus_registry_claim_next_due(&s_registry, MODBUS_BUS_RS485_1, 2000U, &target),
                  -ENOENT, "Fresh claim must hold");

    zassert_equal(modbus_registry_claim_next_due(&s_registry, MODBUS_BUS_RS485_1,
                                                 1000U + MODBUS_CLAIM_STALE_MS, &target),
                  0, "An abandoned claim must be recovered");
}

ZTEST(modbus_registry_suite, test_record_updates_health_and_rejects_unknown_device)
{
    reset_registry();

    modbus_slave_descriptor_t desc = make_desc(1U, MODBUS_BUS_RS485_1, 1U);
    zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Register failed");

    for (uint32_t i = 0U; i < MODBUS_CONSECUTIVE_FAILS_THRESHOLD; i++) {
        zassert_equal(modbus_registry_record(&s_registry, 1U, MODBUS_RESULT_TIMEOUT,
                                             1000U + i),
                      0, "Record failed");
    }

    modbus_slave_snapshot_t snap[1];
    size_t count = 0U;

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap, 1U, &count), 0,
                  "Snapshot failed");
    zassert_equal(snap[0].runtime.health, MODBUS_HEALTH_OFFLINE_BACKOFF,
                  "Three timeouts must trigger backoff");

    zassert_equal(modbus_registry_record(&s_registry, 42U, MODBUS_RESULT_OK, 5000U), -ENOENT,
                  "Unknown device must be rejected");
}

ZTEST(modbus_registry_suite, test_set_bus_moves_device)
{
    reset_registry();

    modbus_slave_descriptor_t desc = make_desc(1U, MODBUS_BUS_RS485_1, 1U);
    zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Register failed");

    zassert_equal(modbus_registry_set_bus(&s_registry, 1U, MODBUS_BUS_RS485_2), 0, "Move failed");

    modbus_slave_snapshot_t snap[4];
    size_t count = 0U;

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap, 4U, &count), 0,
                  "Snapshot failed");
    zassert_equal(count, 0U, "Source bus must be empty");

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_2, snap, 4U, &count), 0,
                  "Snapshot failed");
    zassert_equal(count, 1U, "Target bus must hold the device");

    zassert_equal(modbus_registry_set_bus(&s_registry, 99U, MODBUS_BUS_RS485_2), -ENOENT,
                  "Unknown device must be rejected");
    zassert_equal(modbus_registry_set_bus(&s_registry, 1U, (uint8_t) MODBUS_BUS_MAX), -EINVAL,
                  "Unknown bus must be rejected");
}

ZTEST(modbus_registry_suite, test_set_bus_rejects_address_collision)
{
    reset_registry();

    modbus_slave_descriptor_t moving = make_desc(1U, MODBUS_BUS_RS485_1, 5U);
    modbus_slave_descriptor_t resident = make_desc(2U, MODBUS_BUS_RS485_2, 5U);

    zassert_equal(modbus_registry_register(&s_registry, &moving), 0, "Register failed");
    zassert_equal(modbus_registry_register(&s_registry, &resident), 0, "Register failed");

    zassert_equal(modbus_registry_set_bus(&s_registry, 1U, MODBUS_BUS_RS485_2), -EADDRINUSE,
                  "Failover must not create an address collision");

    modbus_slave_snapshot_t snap[4];
    size_t count = 0U;

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap, 4U, &count), 0,
                  "Snapshot failed");
    zassert_equal(count, 1U, "Rejected failover must leave the device in place");
}

ZTEST(modbus_registry_suite, test_export_import_roundtrip)
{
    reset_registry();

    modbus_slave_descriptor_t first = make_desc(1U, MODBUS_BUS_RS485_1, 1U);
    modbus_slave_descriptor_t second = make_desc(2U, MODBUS_BUS_RS485_2, 5U);

    zassert_equal(modbus_registry_register(&s_registry, &first), 0, "Register failed");
    zassert_equal(modbus_registry_register(&s_registry, &second), 0, "Register failed");

    uint8_t blob[MODBUS_REGISTRY_EXPORT_SIZE_MAX];
    size_t size = 0U;

    zassert_equal(modbus_registry_export(&s_registry, blob, sizeof(blob), &size), 0, "Export failed");
    zassert_equal(size, MODBUS_REGISTRY_EXPORT_HEADER_SIZE + (2U * 9U), "Exported size mismatch");

    uint8_t too_small[4];
    size_t unused = 0U;
    zassert_equal(modbus_registry_export(&s_registry, too_small, sizeof(too_small), &unused),
                  -ENOSPC, "A short buffer must be reported");

    /* Wipe the table, then restore it from the blob. */
    zassert_equal(modbus_registry_init(&s_registry, k_bus_cfg), 0, "Re-init failed");
    zassert_equal(modbus_registry_import(&s_registry, blob, size), 0, "Import failed");

    modbus_slave_snapshot_t snap[4];
    size_t count = 0U;

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap, 4U, &count), 0,
                  "Snapshot failed");
    zassert_equal(count, 1U, "Bus 1 device lost in roundtrip");
    zassert_equal(snap[0].descriptor.slave_addr, 1U, "Address mismatch");

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_2, snap, 4U, &count), 0,
                  "Snapshot failed");
    zassert_equal(count, 1U, "Bus 2 device lost in roundtrip");
    zassert_equal(snap[0].descriptor.device_id, 2U, "Device id mismatch");
    zassert_equal(snap[0].descriptor.dev_type, MODBUS_DEV_TYPE_CWT_TM16PT, "Type mismatch");
}

ZTEST(modbus_registry_suite, test_import_rejects_corrupt_blob_without_mutating)
{
    reset_registry();

    modbus_slave_descriptor_t first = make_desc(1U, MODBUS_BUS_RS485_1, 10U);
    modbus_slave_descriptor_t second = make_desc(2U, MODBUS_BUS_RS485_1, 11U);

    zassert_equal(modbus_registry_register(&s_registry, &first), 0, "Register failed");
    zassert_equal(modbus_registry_register(&s_registry, &second), 0, "Register failed");

    uint8_t blob[MODBUS_REGISTRY_EXPORT_SIZE_MAX];
    size_t size = 0U;

    zassert_equal(modbus_registry_export(&s_registry, blob, sizeof(blob), &size), 0, "Export failed");
    zassert_equal(size, MODBUS_REGISTRY_EXPORT_HEADER_SIZE + (2U * 9U), "Exported size mismatch");

    /* Unknown format version. */
    uint8_t corrupted[MODBUS_REGISTRY_EXPORT_SIZE_MAX];

    (void) memcpy(corrupted, blob, size);
    corrupted[0] = (uint8_t) (MODBUS_REGISTRY_EXPORT_VERSION + 1U);
    zassert_equal(modbus_registry_import(&s_registry, corrupted, size), -EINVAL,
                  "Unknown version must be rejected");

    /* Truncated blob. */
    zassert_equal(modbus_registry_import(&s_registry, blob, size - 1U), -EINVAL,
                  "Truncated blob must be rejected");

    /* Duplicate slave address on the same bus inside the blob: record 1 is the
     * one at HEADER + record size, and its slave address is its third byte. */
    (void) memcpy(corrupted, blob, size);
    corrupted[MODBUS_REGISTRY_EXPORT_HEADER_SIZE + MODBUS_REGISTRY_EXPORT_RECORD_SIZE + 2U] =
        corrupted[MODBUS_REGISTRY_EXPORT_HEADER_SIZE + 2U];

    zassert_equal(modbus_registry_import(&s_registry, corrupted, size), -EINVAL,
                  "Duplicate address inside the blob must be rejected");

    modbus_slave_snapshot_t snap[4];
    size_t count = 0U;

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap, 4U, &count), 0,
                  "Snapshot failed");
    zassert_equal(count, 2U, "A rejected import must leave the table untouched");
    zassert_equal(snap[0].descriptor.device_id, 1U, "Table was mutated by a rejected import");
    zassert_equal(snap[1].descriptor.slave_addr, 11U, "Table was mutated by a rejected import");
}

ZTEST(modbus_registry_suite, test_port_forwards_calls)
{
    reset_registry();

    i_slave_registry_t port;

    zassert_equal(modbus_registry_port(&s_registry, &port), 0, "Port binding failed");
    zassert_true(i_slave_registry_is_bound(&port), "Port must be bound");
    zassert_equal(modbus_registry_port(NULL, &port), -EINVAL, "NULL registry must be rejected");

    modbus_slave_descriptor_t desc = make_desc(1U, MODBUS_BUS_RS485_1, 1U);

    zassert_equal(i_slave_registry_register_device(&port, &desc), 0, "Port register failed");

    modbus_poll_target_t target;
    zassert_equal(i_slave_registry_claim_next_due(&port, MODBUS_BUS_RS485_1, 0U, &target), 0,
                  "Port claim failed");
    zassert_equal(i_slave_registry_record(&port, target.device_id, MODBUS_RESULT_OK, 0U), 0,
                  "Port record failed");

    modbus_slave_snapshot_t snap[2];
    size_t count = 0U;
    zassert_equal(i_slave_registry_snapshot(&port, MODBUS_BUS_RS485_1, snap, 2U, &count), 0,
                  "Port snapshot failed");
    zassert_equal(count, 1U, "Port snapshot count mismatch");

    modbus_bus_cfg_t cfg;
    zassert_equal(i_slave_registry_get_bus_cfg(&port, MODBUS_BUS_RS485_2, &cfg), 0,
                  "Port get_bus_cfg failed");
    zassert_equal(cfg.baudrate, 19200U, "Port bus config mismatch");

    zassert_equal(i_slave_registry_set_bus(&port, 1U, MODBUS_BUS_RS485_2), 0, "Port set_bus failed");
}

ZTEST(modbus_registry_suite, test_next_due_in_reports_wait)
{
    reset_registry();

    modbus_slave_descriptor_t first = make_desc(1U, MODBUS_BUS_RS485_1, 1U);
    modbus_slave_descriptor_t second = make_desc(2U, MODBUS_BUS_RS485_1, 2U);

    zassert_equal(modbus_registry_register(&s_registry, &first), 0, "Register failed");
    zassert_equal(modbus_registry_register(&s_registry, &second), 0, "Register failed");

    uint32_t wait_ms = 12345U;

    zassert_equal(modbus_registry_next_due_in(&s_registry, MODBUS_BUS_RS485_1, 0U, &wait_ms), 0,
                  "next_due_in failed");
    zassert_equal(wait_ms, 0U, "A never polled slave is due now");

    zassert_equal(modbus_registry_record(&s_registry, 1U, MODBUS_RESULT_OK, 0U), 0, "Record failed");
    zassert_equal(modbus_registry_record(&s_registry, 2U, MODBUS_RESULT_OK, 0U), 0, "Record failed");

    zassert_equal(modbus_registry_next_due_in(&s_registry, MODBUS_BUS_RS485_1, 0U, &wait_ms), 0,
                  "next_due_in failed");
    zassert_equal(wait_ms, 1000U, "Both slaves are due in 1000 ms");

    zassert_equal(modbus_registry_next_due_in(&s_registry, MODBUS_BUS_RS485_1, 500U, &wait_ms), 0,
                  "next_due_in failed");
    zassert_equal(wait_ms, 500U, "Remaining interval not reported");

    zassert_equal(modbus_registry_next_due_in(&s_registry, MODBUS_BUS_RS485_1, 1000U, &wait_ms), 0,
                  "next_due_in failed");
    zassert_equal(wait_ms, 0U, "Both slaves are due at the interval boundary");

    /* An in-flight claim must not be counted as due before it goes stale. */
    modbus_poll_target_t target;
    zassert_equal(modbus_registry_claim_next_due(&s_registry, MODBUS_BUS_RS485_1, 1000U, &target),
                  0, "Claim failed");
    zassert_equal(target.device_id, 1U, "Cursor must hand out the first slave");

    zassert_equal(modbus_registry_record(&s_registry, 2U, MODBUS_RESULT_OK, 1000U), 0,
                  "Record failed");

    zassert_equal(modbus_registry_next_due_in(&s_registry, MODBUS_BUS_RS485_1, 1100U, &wait_ms), 0,
                  "next_due_in failed");
    zassert_equal(wait_ms, 900U, "The other slave is due at 2000 ms");

    zassert_equal(modbus_registry_next_due_in(&s_registry, MODBUS_BUS_RS485_1, 0U, NULL), -EINVAL,
                  "NULL out must be rejected");
    zassert_equal(modbus_registry_next_due_in(&s_registry, (uint8_t) MODBUS_BUS_MAX, 0U, &wait_ms),
                  -EINVAL, "Unknown bus must be rejected");
    zassert_equal(modbus_registry_next_due_in(&s_registry, MODBUS_BUS_RS485_2, 0U, &wait_ms),
                  -ENOENT, "An empty bus has nothing to wait for");
}

/* =========================================================================
 * Capacity paths that only a full bus can reach.
 * ========================================================================= */

/** Fill MODBUS_BUS_RS485_2 (16 slots) with device ids 1..16 at addresses 1..16. */
static void fill_second_bus(void)
{
    for (uint8_t i = 0U; i < MODBUS_MAX_SLAVES_PER_BUS; i++) {
        modbus_slave_descriptor_t desc = make_desc(i + 1U, MODBUS_BUS_RS485_2, i + 1U);

        zassert_equal(modbus_registry_register(&s_registry, &desc), 0,
                      "Filling slave %u failed", i + 1U);
    }
}

ZTEST(modbus_registry_suite, test_update_to_a_full_bus_is_rejected)
{
    reset_registry();
    fill_second_bus();

    modbus_slave_descriptor_t moving = make_desc(100U, MODBUS_BUS_RS485_1, 200U);

    zassert_equal(modbus_registry_register(&s_registry, &moving), 0, "Register failed");

    /* Same device, new identity on a bus that already holds 16 slaves. */
    moving.bus_id = MODBUS_BUS_RS485_2;

    zassert_equal(modbus_registry_register(&s_registry, &moving), -ENOSPC,
                  "Moving a device onto a full bus must be rejected");

    modbus_slave_snapshot_t snap[MODBUS_MAX_SLAVES_PER_BUS];
    size_t count = 0U;

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap,
                                          MODBUS_MAX_SLAVES_PER_BUS, &count),
                  0, "Snapshot failed");
    zassert_equal(count, 1U, "A rejected update must leave the device where it was");
    zassert_equal(snap[0].descriptor.slave_addr, 200U, "Descriptor was mutated");
}

ZTEST(modbus_registry_suite, test_set_bus_to_a_full_bus_is_rejected)
{
    reset_registry();
    fill_second_bus();

    modbus_slave_descriptor_t moving = make_desc(100U, MODBUS_BUS_RS485_1, 200U);

    zassert_equal(modbus_registry_register(&s_registry, &moving), 0, "Register failed");
    zassert_equal(modbus_registry_set_bus(&s_registry, 100U, MODBUS_BUS_RS485_2), -ENOSPC,
                  "Failover onto a full bus must be rejected");

    modbus_slave_descriptor_t out;

    zassert_equal(modbus_registry_get(&s_registry, 100U, &out), 0, "Get failed");
    zassert_equal(out.bus_id, MODBUS_BUS_RS485_1, "A rejected failover must not move the device");
}

ZTEST(modbus_registry_suite, test_snapshot_truncates_at_max)
{
    reset_registry();

    for (uint8_t i = 0U; i < 3U; i++) {
        modbus_slave_descriptor_t desc = make_desc(i + 1U, MODBUS_BUS_RS485_1, i + 1U);

        zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Register failed");
    }

    modbus_slave_snapshot_t snap[2];
    size_t count = 0U;

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap, 2U, &count), 0,
                  "Snapshot failed");
    zassert_equal(count, 2U, "Snapshot must report only what it copied");
}

ZTEST(modbus_registry_suite, test_next_due_in_ignores_disabled_slaves)
{
    reset_registry();

    modbus_slave_descriptor_t desc = make_desc(1U, MODBUS_BUS_RS485_2, 1U);

    desc.is_enabled = false;
    zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Register failed");

    uint32_t wait_ms = 0U;

    zassert_equal(modbus_registry_next_due_in(&s_registry, MODBUS_BUS_RS485_2, 0U, &wait_ms),
                  -ENOENT, "A bus with no enabled slave has nothing to wait for");
}

/* =========================================================================
 * Import capacity rejections.
 * ========================================================================= */

#define TEST_EXPORT_HEADER 18U
#define TEST_EXPORT_RECORD 9U

/** Write the fixed part of an export blob: version, buses and record count. */
static void write_blob_header(uint8_t *blob, uint16_t record_count)
{
    blob[0] = (uint8_t) MODBUS_REGISTRY_EXPORT_VERSION;
    blob[1] = (uint8_t) MODBUS_BUS_MAX;

    size_t offset = 2U;

    for (uint8_t bus = 0U; bus < (uint8_t) MODBUS_BUS_MAX; bus++) {
        blob[offset++] = 0x80U; /* 9600 = 0x2580, little-endian */
        blob[offset++] = 0x25U;
        blob[offset++] = 0x00U;
        blob[offset++] = 0x00U;
        blob[offset++] = (uint8_t) MODBUS_PARITY_NONE;
        blob[offset++] = 1U;
        blob[offset++] = 8U;
    }

    blob[offset++] = (uint8_t) (record_count & 0xFFU);
    blob[offset++] = (uint8_t) (record_count >> 8);
}

/** Write one record in the layout the import expects. */
static void write_blob_record(uint8_t *blob, size_t index, uint8_t device_id, uint8_t bus_id,
                             uint8_t slave_addr)
{
    uint8_t *record = &blob[TEST_EXPORT_HEADER + (index * TEST_EXPORT_RECORD)];

    record[0] = device_id;
    record[1] = bus_id;
    record[2] = slave_addr;
    record[3] = (uint8_t) MODBUS_DEV_TYPE_CWT_TM16PT;
    record[4] = 0xE8U; /* 1000 ms */
    record[5] = 0x03U;
    record[6] = 0x2CU; /* 300 ms */
    record[7] = 0x01U;
    record[8] = 1U;
}

ZTEST(modbus_registry_suite, test_import_rejects_more_records_than_the_table_holds)
{
    reset_registry();

    const uint16_t records = (uint16_t) (MODBUS_MAX_TOTAL_SLAVES + 1U);
    uint8_t blob[TEST_EXPORT_HEADER + ((MODBUS_MAX_TOTAL_SLAVES + 1U) * TEST_EXPORT_RECORD)];

    (void) memset(blob, 0, sizeof(blob));
    write_blob_header(blob, records);

    zassert_equal(modbus_registry_import(&s_registry, blob, sizeof(blob)), -EINVAL,
                  "A blob with more records than slots must be rejected");
}

ZTEST(modbus_registry_suite, test_import_rejects_a_bus_over_its_capacity)
{
    reset_registry();

    const uint16_t records = (uint16_t) (MODBUS_MAX_SLAVES_PER_BUS + 1U);
    uint8_t blob[TEST_EXPORT_HEADER + ((MODBUS_MAX_SLAVES_PER_BUS + 1U) * TEST_EXPORT_RECORD)];

    (void) memset(blob, 0, sizeof(blob));
    write_blob_header(blob, records);

    for (uint16_t i = 0U; i < records; i++) {
        write_blob_record(blob, i, (uint8_t) (i + 1U), MODBUS_BUS_RS485_1, (uint8_t) (i + 1U));
    }

    zassert_equal(modbus_registry_import(&s_registry, blob, sizeof(blob)), -EINVAL,
                  "A blob that overfills one bus must be rejected");
}

/* =========================================================================
 * Concurrency: two polling threads must never claim the same slave.
 * ========================================================================= */

#define CONCURRENCY_THREADS 2
#define CONCURRENCY_SLAVES  4
#define CONCURRENCY_LOOPS   500

static atomic_t s_claim_flag[MODBUS_MAX_TOTAL_SLAVES + 1U];
static atomic_t s_double_claims;
static atomic_t s_claims;
static atomic_t s_records;

static void registry_worker(void *p1, void *p2, void *p3)
{
    modbus_registry_t *registry = (modbus_registry_t *) p1;
    uint64_t now_ms = (uint64_t) (uintptr_t) p2;

    ARG_UNUSED(p3);

    for (uint32_t i = 0U; i < CONCURRENCY_LOOPS; i++) {
        modbus_poll_target_t target;

        if (modbus_registry_claim_next_due(registry, MODBUS_BUS_RS485_1, now_ms + i, &target) != 0) {
            continue;
        }

        if (!atomic_cas(&s_claim_flag[target.device_id], 0, 1)) {
            atomic_inc(&s_double_claims);
        }

        atomic_inc(&s_claims);

        /* Small window to expose a missing lock. */
        k_yield();

        (void) modbus_registry_record(registry, target.device_id, MODBUS_RESULT_OK, now_ms + i);
        atomic_clear(&s_claim_flag[target.device_id]);
        atomic_inc(&s_records);
    }
}

K_THREAD_STACK_ARRAY_DEFINE(s_worker_stack, CONCURRENCY_THREADS, 2048);
static struct k_thread s_worker_thread[CONCURRENCY_THREADS];

ZTEST(modbus_registry_suite, test_two_threads_never_claim_the_same_slave)
{
    reset_registry();

    for (uint8_t i = 0U; i < CONCURRENCY_SLAVES; i++) {
        modbus_slave_descriptor_t desc = make_desc(i + 1U, MODBUS_BUS_RS485_1, i + 1U);

        zassert_equal(modbus_registry_register(&s_registry, &desc), 0, "Register failed");
    }

    atomic_clear(&s_double_claims);
    atomic_clear(&s_claims);
    atomic_clear(&s_records);

    for (uint32_t i = 0U; i <= CONCURRENCY_SLAVES; i++) {
        atomic_clear(&s_claim_flag[i]);
    }

    for (uint32_t i = 0U; i < CONCURRENCY_THREADS; i++) {
        k_thread_create(&s_worker_thread[i], s_worker_stack[i],
                        K_THREAD_STACK_SIZEOF(s_worker_stack[i]), registry_worker, &s_registry,
                        (void *) (uintptr_t) 0, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
    }

    for (uint32_t i = 0U; i < CONCURRENCY_THREADS; i++) {
        k_thread_join(&s_worker_thread[i], K_FOREVER);
    }

    zassert_equal(atomic_get(&s_double_claims), 0, "Two threads claimed the same slave");
    zassert_equal(atomic_get(&s_claims), atomic_get(&s_records),
                  "Every claim must be released by a record");
    zassert_true(atomic_get(&s_claims) > 0U, "The workers never claimed anything");

    modbus_slave_snapshot_t snap[CONCURRENCY_SLAVES];
    size_t count = 0U;

    zassert_equal(modbus_registry_snapshot(&s_registry, MODBUS_BUS_RS485_1, snap,
                                           CONCURRENCY_SLAVES, &count),
                  0, "Snapshot failed");
    zassert_equal(count, CONCURRENCY_SLAVES, "Slave count changed under concurrency");

    uint32_t total_ok = 0U;

    for (size_t i = 0U; i < count; i++) {
        zassert_false(snap[i].runtime.in_flight, "No slave may stay claimed");
        zassert_equal(snap[i].runtime.health, MODBUS_HEALTH_ONLINE, "No slave may degrade");
        total_ok += snap[i].runtime.successful_polls;
    }

    zassert_equal(total_ok, (uint32_t) atomic_get(&s_records),
                  "Successful poll counter lost an update");
}
