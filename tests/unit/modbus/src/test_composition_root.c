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

static modbus_transaction_result_t fake_read_coils(void *impl, uint8_t slave_addr,
                                                   uint16_t coil_addr, uint16_t quantity,
                                                   uint8_t *out, int32_t timeout_ms)
{
    fake_client_t *fake = (fake_client_t *) impl;

    fake->calls++;
    if (slave_addr < 16U) {
        fake->calls_by_addr[slave_addr]++;
    }
    fake->last_addr = slave_addr;
    fake->last_reg = coil_addr;
    fake->quantity = quantity;
    fake->last_timeout = timeout_ms;

    if (fake->next_result == MODBUS_RESULT_OK) {
        size_t bytes = ((size_t) quantity + 7U) / 8U;
        (void) memset(out, 0, bytes);
    }

    return fake->next_result;
}

static modbus_transaction_result_t fake_write_single_coil(void *impl, uint8_t slave_addr,
                                                          uint16_t coil_addr, uint16_t value,
                                                          int32_t timeout_ms)
{
    fake_client_t *fake = (fake_client_t *) impl;

    fake->calls++;
    if (slave_addr < 16U) {
        fake->calls_by_addr[slave_addr]++;
    }
    fake->last_addr = slave_addr;
    fake->last_reg = coil_addr;
    fake->last_timeout = timeout_ms;

    return MODBUS_RESULT_OK;
}

static modbus_transaction_result_t fake_write_multiple_coils(void *impl, uint8_t slave_addr,
                                                             uint16_t coil_addr, uint16_t quantity,
                                                             const uint8_t *values, int32_t timeout_ms)
{
    ARG_UNUSED(impl);
    ARG_UNUSED(slave_addr);
    ARG_UNUSED(coil_addr);
    ARG_UNUSED(quantity);
    ARG_UNUSED(values);
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
    .read_coils = fake_read_coils,
    .write_single_coil = fake_write_single_coil,
    .write_multiple_coils = fake_write_multiple_coils,
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
                  APP_CONTAINER_INIT_REGISTRY | APP_CONTAINER_INIT_AO |
                  APP_CONTAINER_INIT_STORAGE | APP_CONTAINER_INIT_NETWORK |
                  APP_CONTAINER_INIT_INDICATORS | APP_CONTAINER_INIT_DIGITAL_INPUT,
                  "Init mask mismatch");
    zassert_true(i_slave_registry_is_bound(&s_app.registry), "Registry port must be bound");
    zassert_true(i_config_storage_is_bound(&s_app.storage), "Storage port must be bound");
    zassert_true(i_led_is_bound(&s_app.led), "LED port must be bound");
    zassert_true(i_buzzer_is_bound(&s_app.buzzer), "Buzzer port must be bound");
    zassert_true(i_digital_input_is_bound(&s_app.digital_inputs), "Digital input port must be bound");

    modbus_slave_snapshot_t snap[5];
    size_t count = 0U;

    zassert_equal(i_slave_registry_snapshot(&s_app.registry, MODBUS_BUS_RS485_1, snap, 5U, &count),
                  0, "Snapshot failed");
    zassert_equal(count, 4U, "The default devices must be registered");
    zassert_equal(snap[0].descriptor.dev_type, MODBUS_DEV_TYPE_CWT_TM16PT, "Device 1 type mismatch");
    zassert_equal(snap[1].descriptor.dev_type, MODBUS_DEV_TYPE_JSY_MK339, "Device 2 type mismatch");
    zassert_equal(snap[2].descriptor.dev_type, MODBUS_DEV_TYPE_MBSL32DI, "Device 3 type mismatch");
    zassert_equal(snap[3].descriptor.dev_type, MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH, "Device 4 type mismatch");

    /* Relay ports verification */
    zassert_true(i_relay_is_bound(&s_app.relay_ext), "relay_ext must be bound");
    zassert_false(i_relay_is_bound(&s_app.relay_int), "relay_int placeholder must not be bound yet");

    relay_state_t rstate = RELAY_STATE_CLOSED;
    zassert_equal(i_relay_get_state(&s_app.relay_ext, RELAY_CHANNEL_0, &rstate), 0, "get_state failed");
    zassert_equal(rstate, RELAY_STATE_OPEN, "Initial state for relay 0 must be OPEN");

    zassert_equal(i_relay_set_state(&s_app.relay_ext, RELAY_CHANNEL_0, RELAY_STATE_CLOSED), 0, "set_state failed");
    zassert_equal(i_relay_get_state(&s_app.relay_ext, RELAY_CHANNEL_0, &rstate), 0, "get_state failed");
    zassert_equal(rstate, RELAY_STATE_CLOSED, "Relay 0 must now be CLOSED in cache");

    /* Network ports and AO verification */
    zassert_true(i_network_is_bound(&s_app.net_eth), "net_eth port must be bound");
    zassert_true(i_network_is_bound(&s_app.net_wifi), "net_wifi port must be bound");
    zassert_equal(s_app.ao_net.eth_cfg.ip_mode, NET_IP_MODE_DHCP, "ETH default ip_mode must be DHCP");
    zassert_equal(s_app.ao_net.eth_cfg.ip_addr, 0xC0A80164, "ETH fallback IP mismatch");
    zassert_equal(s_app.ao_net.wifi_cfg.mode, NET_WIFI_MODE_AP, "WiFi default mode must be AP");
    zassert_str_equal(s_app.ao_net.wifi_cfg.ap.ssid, "BrightC-AP", "WiFi default SSID mismatch");

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

    zassert_true(s_fake.calls >= 4U, "The polling thread never reached the client");
    zassert_true(s_fake.calls_by_addr[1] > 0U, "Slave 1 was never polled");
    zassert_true(s_fake.calls_by_addr[2] > 0U, "Slave 2 was never polled");
    zassert_true(s_fake.calls_by_addr[3] > 0U, "Slave 3 was never polled");
    zassert_true(s_fake.calls_by_addr[4] > 0U, "Slave 4 was never polled");
    zassert_equal(s_fake.last_timeout, 300, "Timeout not taken from the descriptor");

    modbus_slave_snapshot_t snap[5];
    size_t count = 0U;

    zassert_equal(i_slave_registry_snapshot(&s_app.registry, MODBUS_BUS_RS485_1, snap, 5U, &count),
                  0, "Snapshot failed");
    zassert_equal(count, 4U, "Snapshot count mismatch");
    zassert_equal(snap[0].runtime.health, MODBUS_HEALTH_ONLINE, "A successful poll must be ONLINE");
    zassert_true(snap[0].runtime.successful_polls > 0U, "Success counter not updated");
    zassert_false(snap[0].runtime.in_flight, "The claim was not released");

    zassert_equal(snap[1].runtime.health, MODBUS_HEALTH_ONLINE, "A successful poll must be ONLINE");
    zassert_true(snap[1].runtime.successful_polls > 0U, "Success counter not updated");
    zassert_false(snap[1].runtime.in_flight, "The claim was not released");

    zassert_equal(snap[2].runtime.health, MODBUS_HEALTH_ONLINE, "A successful poll must be ONLINE");
    zassert_true(snap[2].runtime.successful_polls > 0U, "Success counter not updated");
    zassert_false(snap[2].runtime.in_flight, "The claim was not released");

    zassert_equal(snap[3].runtime.health, MODBUS_HEALTH_ONLINE, "A successful poll must be ONLINE");
    zassert_true(snap[3].runtime.successful_polls > 0U, "Success counter not updated");
    zassert_false(snap[3].runtime.in_flight, "The claim was not released");

    /* Failures must degrade the slave and reach the registry. */
    s_fake.next_result = MODBUS_RESULT_TIMEOUT;
    k_msleep(1200);

    zassert_equal(i_slave_registry_snapshot(&s_app.registry, MODBUS_BUS_RS485_1, snap, 5U, &count),
                  0, "Snapshot failed");
    zassert_equal(count, 4U, "Snapshot count mismatch after failure");
    zassert_true(snap[0].runtime.timeout_count > 0U, "Timeouts not recorded");
    zassert_not_equal(snap[0].runtime.health, MODBUS_HEALTH_ONLINE, "Timeouts must degrade");
    zassert_true(snap[1].runtime.timeout_count > 0U, "Timeouts not recorded");
    zassert_not_equal(snap[1].runtime.health, MODBUS_HEALTH_ONLINE, "Timeouts must degrade");
    zassert_true(snap[2].runtime.timeout_count > 0U, "Timeouts not recorded");
    zassert_not_equal(snap[2].runtime.health, MODBUS_HEALTH_ONLINE, "Timeouts must degrade");
    zassert_true(snap[3].runtime.timeout_count > 0U, "Timeouts not recorded");
    zassert_not_equal(snap[3].runtime.health, MODBUS_HEALTH_ONLINE, "Timeouts must degrade");

    zassert_equal(ao_modbus_stop(&s_app.ao[0]), 0, "Stop failed");
    zassert_equal(ao_modbus_stop(&s_app.ao[1]), 0, "Stop failed");
    zassert_equal(ao_storage_stop(&s_app.ao_store), 0, "Stop ao_store failed");
    zassert_equal(ao_network_stop(&s_app.ao_net), 0, "Stop ao_network failed");
    zassert_equal(ao_system_indicator_stop(&s_app.ao_indicator), 0, "Stop ao_indicator failed");
    zassert_equal(ao_digital_input_stop(&s_app.ao_di), 0, "Stop ao_di failed");
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

    zassert_equal(composition_root_stop(&s_app), 0, "composition_root_stop failed");

    /* A stopped thread must be silent. */
    const uint32_t calls_after_stop = s_fake.calls;

    k_msleep(100);
    zassert_equal(s_fake.calls, calls_after_stop, "A stopped thread must not poll");

    /* Restarting must not inherit a stale STOP from the previous run. */
    zassert_equal(ao_modbus_start(&s_app.ao[0]), 0, "Restart failed");
    zassert_equal(ao_storage_start(&s_app.ao_store), 0, "Restart ao_store failed");
    zassert_equal(ao_network_start(&s_app.ao_net), 0, "Restart ao_network failed");
    zassert_equal(ao_system_indicator_start(&s_app.ao_indicator), 0, "Restart ao_indicator failed");
    zassert_equal(ao_digital_input_start(&s_app.ao_di), 0, "Restart ao_di failed");

    k_msleep(1200); /* The default slave polls every 1000 ms. */

    zassert_true(s_fake.calls > calls_after_stop, "A restarted thread must poll again");

    zassert_equal(composition_root_stop(&s_app), 0, "composition_root_stop failed");
}

ZTEST(composition_root_suite, test_digital_inputs_bound_and_queryable)
{
    (void) memset(&s_app, 0, sizeof(s_app));

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");
    zassert_true(i_digital_input_is_bound(&s_app.digital_inputs), "Digital input port must be bound");

    bool is_active = true;
    zassert_equal(i_digital_input_get_state(&s_app.digital_inputs, DI_ROLE_USER_BUTTON, &is_active), 0,
                  "Query DI_ROLE_USER_BUTTON state failed");
    zassert_false(is_active, "Initial button state must be inactive");

    zassert_equal(i_digital_input_get_state(&s_app.digital_inputs, DI_ROLE_BYPASS_SWITCH, &is_active), 0,
                  "Query DI_ROLE_BYPASS_SWITCH state failed");
    zassert_false(is_active, "Initial bypass switch state must be inactive");

    bool raw_level = true;
    zassert_equal(i_digital_input_get_raw_level(&s_app.digital_inputs, DI_ROLE_USER_BUTTON, &raw_level), 0,
                  "Query raw level failed");
    zassert_false(raw_level, "Initial raw level must be false");

    uint64_t mask = 0xFFFFFFFFULL;
    zassert_equal(i_digital_input_get_active_mask(&s_app.digital_inputs, &mask), 0,
                  "Query active mask failed");
    zassert_equal(mask, 0ULL, "Active mask must initially be 0");
}
