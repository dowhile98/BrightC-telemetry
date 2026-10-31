/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Composition-root smoke test: the container must wire the registry and start
 * one polling thread per bus, and the polling loop must reach the client port.
 * Runs on native_sim; no hardware and no nanoMODBUS.
 */

#include <zephyr/ztest.h>

#include <errno.h>
#include <string.h>

#include "infrastructure/di/composition_root.h"

#define FAKE_BUS 0U

typedef struct {
    modbus_transaction_result_t next_result; /**< Result returned by the next poll */
    uint32_t calls;        /**< Number of polls performed */
    uint32_t calls_by_addr[16]; /**< Number of polls per slave address */
    uint8_t last_addr;     /**< Last address polled */
    uint16_t last_reg;     /**< Last register address polled */
    uint16_t quantity;     /**< Last register count polled */
    int32_t last_timeout;  /**< Last timeout received */
} fake_client_t;

static fake_client_t s_fake;

static modbus_transaction_result_t fake_read_holding(void *impl, uint8_t slave_addr,
                                                     uint16_t reg_addr, uint16_t quantity,
                                                     uint16_t *out, int32_t timeout_ms)
{
    fake_client_t *fake = (fake_client_t *) impl;

    fake->calls++;
    if (slave_addr < 16U) {
        fake->calls_by_addr[slave_addr]++;
    }
    fake->last_addr = slave_addr;
    fake->last_reg = reg_addr;
    fake->quantity = quantity;
    fake->last_timeout = timeout_ms;

    if (fake->next_result == MODBUS_RESULT_OK) {
        for (uint16_t i = 0U; i < quantity; i++) {
            out[i] = 250U; /* 25.0 C */
        }
    }

    return fake->next_result;
}

static modbus_transaction_result_t fake_write_single(void *impl, uint8_t slave_addr,
                                                     uint16_t reg_addr, uint16_t value,
                                                     int32_t timeout_ms)
{
    ARG_UNUSED(impl);
    ARG_UNUSED(slave_addr);
    ARG_UNUSED(reg_addr);
    ARG_UNUSED(value);
    ARG_UNUSED(timeout_ms);

    return MODBUS_RESULT_OK;
}

static int fake_set_cfg(void *impl, const modbus_bus_cfg_t *cfg)
{
    ARG_UNUSED(impl);
    ARG_UNUSED(cfg);

    return 0;
}

static int fake_get_cfg(void *impl, modbus_bus_cfg_t *out)
{
    ARG_UNUSED(impl);

    out->baudrate = 9600U;
    out->parity = MODBUS_PARITY_NONE;
    out->stop_bits = 1U;
    out->data_bits = 8U;

    return 0;
}

static const i_modbus_client_api_t k_fake_api = {
    .read_holding_registers = fake_read_holding,
    .write_single_register = fake_write_single,
    .set_bus_cfg = fake_set_cfg,
    .get_bus_cfg = fake_get_cfg,
};

/* The container is ~10 KB: keep it off the ztest stack. */
static app_container_t s_app;

ZTEST_SUITE(composition_root_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(composition_root_suite, test_init_rejects_bad_arguments)
{
    zassert_equal(composition_root_init(NULL), -EINVAL, "NULL container must be rejected");
    zassert_equal(composition_root_start(NULL), -EINVAL, "NULL container must be rejected");
}

ZTEST(composition_root_suite, test_init_wires_registry_and_default_device)
{
    (void) memset(&s_app, 0, sizeof(s_app));

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");
    zassert_equal(s_app.init_mask,
                  APP_CONTAINER_INIT_REGISTRY | APP_CONTAINER_INIT_AO, "Init mask mismatch");
    zassert_true(i_slave_registry_is_bound(&s_app.registry), "Registry port must be bound");

    modbus_slave_snapshot_t snap[4];
    size_t count = 0U;

    zassert_equal(i_slave_registry_snapshot(&s_app.registry, MODBUS_BUS_RS485_1, snap, 4U, &count),
                  0, "Snapshot failed");
    zassert_equal(count, 2U, "The default devices must be registered");
    zassert_equal(snap[0].descriptor.dev_type, MODBUS_DEV_TYPE_CWT_TM16PT, "Device 1 type mismatch");
    zassert_equal(snap[1].descriptor.dev_type, MODBUS_DEV_TYPE_JSY_MK339, "Device 2 type mismatch");

    /* Idempotent. */
    zassert_equal(composition_root_init(&s_app), 0, "Second init must be a no-op");
}

ZTEST(composition_root_suite, test_start_runs_both_threads_and_polls_the_bound_bus)
{
    (void) memset(&s_app, 0, sizeof(s_app));
    (void) memset(&s_fake, 0, sizeof(s_fake));

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");

    /* The transport is bound after the container exists, as the adapter is
     * created by the composition root in the firmware build. */
    s_app.client[FAKE_BUS].api = &k_fake_api;
    s_app.client[FAKE_BUS].impl = &s_fake;

    zassert_equal(composition_root_start(&s_app), 0, "Start failed");

    /* Let the polling threads run a few cycles. */
    k_msleep(100);

    zassert_true(s_fake.calls >= 2U, "The polling thread never reached the client");
    zassert_true(s_fake.calls_by_addr[1] > 0U, "Slave 1 was never polled");
    zassert_true(s_fake.calls_by_addr[2] > 0U, "Slave 2 was never polled");
    zassert_equal(s_fake.last_timeout, 300, "Timeout not taken from the descriptor");

    modbus_slave_snapshot_t snap[4];
    size_t count = 0U;

    zassert_equal(i_slave_registry_snapshot(&s_app.registry, MODBUS_BUS_RS485_1, snap, 4U, &count),
                  0, "Snapshot failed");
    zassert_equal(count, 2U, "Snapshot count mismatch");
    zassert_equal(snap[0].runtime.health, MODBUS_HEALTH_ONLINE, "A successful poll must be ONLINE");
    zassert_true(snap[0].runtime.successful_polls > 0U, "Success counter not updated");
    zassert_false(snap[0].runtime.in_flight, "The claim was not released");

    zassert_equal(snap[1].runtime.health, MODBUS_HEALTH_ONLINE, "A successful poll must be ONLINE");
    zassert_true(snap[1].runtime.successful_polls > 0U, "Success counter not updated");
    zassert_false(snap[1].runtime.in_flight, "The claim was not released");

    /* Failures must degrade the slave and reach the registry. */
    s_fake.next_result = MODBUS_RESULT_TIMEOUT;
    k_msleep(1200);

    zassert_equal(i_slave_registry_snapshot(&s_app.registry, MODBUS_BUS_RS485_1, snap, 4U, &count),
                  0, "Snapshot failed");
    zassert_equal(count, 2U, "Snapshot count mismatch after failure");
    zassert_true(snap[0].runtime.timeout_count > 0U, "Timeouts not recorded");
    zassert_not_equal(snap[0].runtime.health, MODBUS_HEALTH_ONLINE, "Timeouts must degrade");
    zassert_true(snap[1].runtime.timeout_count > 0U, "Timeouts not recorded");
    zassert_not_equal(snap[1].runtime.health, MODBUS_HEALTH_ONLINE, "Timeouts must degrade");

    zassert_equal(ao_modbus_stop(&s_app.ao[0]), 0, "Stop failed");
    zassert_equal(ao_modbus_stop(&s_app.ao[1]), 0, "Stop failed");
    k_msleep(50);
}

ZTEST(composition_root_suite, test_stop_then_start_keeps_polling)
{
    (void) memset(&s_app, 0, sizeof(s_app));
    (void) memset(&s_fake, 0, sizeof(s_fake));

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");

    s_app.client[FAKE_BUS].api = &k_fake_api;
    s_app.client[FAKE_BUS].impl = &s_fake;

    zassert_equal(composition_root_start(&s_app), 0, "Start failed");
    k_msleep(100);
    zassert_true(s_fake.calls > 0U, "The polling thread never reached the client");

    zassert_equal(ao_modbus_stop(&s_app.ao[0]), 0, "Stop failed");
    zassert_equal(ao_modbus_stop(&s_app.ao[1]), 0, "Stop failed");

    /* A stopped thread must be silent. */
    const uint32_t calls_after_stop = s_fake.calls;

    k_msleep(100);
    zassert_equal(s_fake.calls, calls_after_stop, "A stopped thread must not poll");

    /* Restarting must not inherit a stale STOP from the previous run. */
    zassert_equal(ao_modbus_start(&s_app.ao[0]), 0, "Restart failed");

    k_msleep(1200); /* The default slave polls every 1000 ms. */

    zassert_true(s_fake.calls > calls_after_stop, "A restarted thread must poll again");

    zassert_equal(ao_modbus_stop(&s_app.ao[0]), 0, "Stop failed");
}
