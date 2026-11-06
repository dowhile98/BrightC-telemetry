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

static void composition_root_test_after(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)composition_root_deinit(&s_app);
}

ZTEST_SUITE(composition_root_suite, NULL, NULL, NULL, composition_root_test_after, NULL);

ZTEST(composition_root_suite, test_init_rejects_bad_arguments)
{
    zassert_equal(composition_root_init(NULL), -EINVAL, "NULL container must be rejected");
    zassert_equal(composition_root_start(NULL), -EINVAL, "NULL container must be rejected");
    zassert_equal(composition_root_stop(NULL), -EINVAL, "NULL container must be rejected");
    zassert_equal(composition_root_deinit(NULL), -EINVAL, "NULL container must be rejected");

    /* DIP interface getters with NULL container must return NULL */
    zassert_is_null(app_container_get_registry(NULL), "NULL container must yield NULL registry");
    zassert_is_null(app_container_get_modbus_client(NULL, 0), "NULL container must yield NULL client");
    zassert_is_null(app_container_get_relay(NULL), "NULL container must yield NULL relay");
    zassert_is_null(app_container_get_network_eth(NULL), "NULL container must yield NULL net_eth");
    zassert_is_null(app_container_get_network_wifi(NULL), "NULL container must yield NULL net_wifi");
    zassert_is_null(app_container_get_storage(NULL), "NULL container must yield NULL storage");
    zassert_is_null(app_container_get_digital_inputs(NULL), "NULL container must yield NULL digital_inputs");
    zassert_is_null(app_container_get_led(NULL), "NULL container must yield NULL led");
    zassert_is_null(app_container_get_buzzer(NULL), "NULL container must yield NULL buzzer");
    zassert_is_null(app_container_get_wifi_control(NULL), "NULL container must yield NULL wifi_control");
}

ZTEST(composition_root_suite, test_init_wires_registry_and_default_device)
{
    (void) composition_root_deinit(&s_app);
    (void) memset(&s_app, 0, sizeof(s_app));

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");
    zassert_true(s_app.is_initialized, "is_initialized must be true");
    zassert_true(s_app.modbus.initialized, "modbus subsystem must be initialized");
    zassert_true(s_app.storage.initialized, "storage subsystem must be initialized");
    zassert_true(s_app.network.initialized, "network subsystem must be initialized");
    zassert_true(s_app.indicators.initialized, "indicators subsystem must be initialized");
    zassert_true(s_app.di.initialized, "di subsystem must be initialized");

    /* DIP Getters verification */
    const i_slave_registry_t *registry = app_container_get_registry(&s_app);
    zassert_not_null(registry, "Registry port must not be NULL");
    zassert_true(i_slave_registry_is_bound(registry), "Registry port must be bound");

    const i_config_storage_t *storage = app_container_get_storage(&s_app);
    zassert_not_null(storage, "Storage port must not be NULL");
    zassert_true(i_config_storage_is_bound(storage), "Storage port must be bound");

    const i_led_t *led = app_container_get_led(&s_app);
    zassert_not_null(led, "LED port must not be NULL");
    zassert_true(i_led_is_bound(led), "LED port must be bound");

    const i_buzzer_t *buzzer = app_container_get_buzzer(&s_app);
    zassert_not_null(buzzer, "Buzzer port must not be NULL");
    zassert_true(i_buzzer_is_bound(buzzer), "Buzzer port must be bound");

    const i_digital_input_t *di = app_container_get_digital_inputs(&s_app);
    zassert_not_null(di, "Digital input port must not be NULL");
    zassert_true(i_digital_input_is_bound(di), "Digital input port must be bound");

    modbus_slave_snapshot_t snap[5];
    size_t count = 0U;

    zassert_equal(i_slave_registry_snapshot(registry, MODBUS_BUS_RS485_1, snap, 5U, &count),
                  0, "Snapshot failed");
    zassert_equal(count, 4U, "The default devices must be registered");
    zassert_equal(snap[0].descriptor.dev_type, MODBUS_DEV_TYPE_CWT_TM16PT, "Device 1 type mismatch");
    zassert_equal(snap[1].descriptor.dev_type, MODBUS_DEV_TYPE_JSY_MK339, "Device 2 type mismatch");
    zassert_equal(snap[2].descriptor.dev_type, MODBUS_DEV_TYPE_MBSL32DI, "Device 3 type mismatch");
    zassert_equal(snap[3].descriptor.dev_type, MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH, "Device 4 type mismatch");

    /* Relay ports verification */
    const i_relay_t *relay_ext = app_container_get_relay(&s_app);
    zassert_not_null(relay_ext, "relay_ext must not be NULL");
    zassert_true(i_relay_is_bound(relay_ext), "relay_ext must be bound");
    zassert_false(i_relay_is_bound(&s_app.modbus.relay_int_port), "relay_int placeholder must not be bound yet");

    relay_state_t rstate = RELAY_STATE_CLOSED;
    zassert_equal(i_relay_get_state(relay_ext, RELAY_CHANNEL_0, &rstate), 0, "get_state failed");
    zassert_equal(rstate, RELAY_STATE_OPEN, "Initial state for relay 0 must be OPEN");

    zassert_equal(i_relay_set_state(relay_ext, RELAY_CHANNEL_0, RELAY_STATE_CLOSED), 0, "set_state failed");
    zassert_equal(i_relay_get_state(relay_ext, RELAY_CHANNEL_0, &rstate), 0, "get_state failed");
    zassert_equal(rstate, RELAY_STATE_CLOSED, "Relay 0 must now be CLOSED in cache");

    relay_channel_status_t rstatus;
    zassert_equal(i_relay_get_status(relay_ext, RELAY_CHANNEL_0, &rstatus), 0, "get_status failed");
    zassert_equal(rstatus.state, RELAY_STATE_CLOSED, "Relay 0 status state must be CLOSED");
    zassert_equal(rstatus.cycle_count, 1U, "Relay 0 cycle_count must be 1");
    zassert_false(rstatus.is_fault, "Relay 0 must not be in fault");

    /* Network ports and AO verification */
    const i_network_t *net_eth = app_container_get_network_eth(&s_app);
    const i_network_t *net_wifi = app_container_get_network_wifi(&s_app);
    const i_wifi_control_t *wifi_ctrl = app_container_get_wifi_control(&s_app);
    zassert_not_null(net_eth, "net_eth port must not be NULL");
    zassert_not_null(net_wifi, "net_wifi port must not be NULL");
    zassert_not_null(wifi_ctrl, "wifi_ctrl port must not be NULL");
    zassert_true(i_network_is_bound(net_eth), "net_eth port must be bound");
    zassert_true(i_network_is_bound(net_wifi), "net_wifi port must be bound");
    zassert_true(i_wifi_control_is_bound(wifi_ctrl), "wifi_ctrl port must be bound");
    zassert_equal(s_app.network.ao.eth_cfg.ip_mode, NET_IP_MODE_DHCP, "ETH default ip_mode must be DHCP");
    zassert_equal(s_app.network.ao.eth_cfg.ip_addr, 0xC0A80164, "ETH fallback IP mismatch");
    zassert_equal(s_app.network.ao.wifi_cfg.mode, NET_WIFI_MODE_AP, "WiFi default mode must be AP");
    zassert_str_equal(s_app.network.ao.wifi_cfg.ap.ssid, "BrightC-AP", "WiFi default SSID mismatch");

    /* Idempotent. */
    zassert_equal(composition_root_init(&s_app), 0, "Second init must be a no-op");
    (void) composition_root_deinit(&s_app);
}

ZTEST(composition_root_suite, test_start_runs_both_threads_and_polls_the_bound_bus)
{
    (void) composition_root_deinit(&s_app);
    (void) memset(&s_app, 0, sizeof(s_app));
    (void) memset(&s_fake, 0, sizeof(s_fake));

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");

    /* The transport is bound after the container exists, as the adapter is
     * created by the composition root in the firmware build. */
    s_app.modbus.client_port[FAKE_BUS].api = &k_fake_api;
    s_app.modbus.client_port[FAKE_BUS].impl = &s_fake;

    const i_modbus_client_t *client = app_container_get_modbus_client(&s_app, FAKE_BUS);
    zassert_not_null(client, "Modbus client port must not be NULL");
    zassert_true(i_modbus_client_is_bound(client), "Modbus client port must be bound");

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

    const i_slave_registry_t *registry = app_container_get_registry(&s_app);
    zassert_not_null(registry, "Registry port must not be NULL");

    zassert_equal(i_slave_registry_snapshot(registry, MODBUS_BUS_RS485_1, snap, 5U, &count),
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

    zassert_equal(i_slave_registry_snapshot(registry, MODBUS_BUS_RS485_1, snap, 5U, &count),
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

    zassert_equal(composition_root_stop(&s_app), 0, "composition_root_stop failed");
    (void) composition_root_deinit(&s_app);
}

ZTEST(composition_root_suite, test_stop_then_start_keeps_polling)
{
    (void) composition_root_deinit(&s_app);
    (void) memset(&s_app, 0, sizeof(s_app));
    (void) memset(&s_fake, 0, sizeof(s_fake));

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");

    s_app.modbus.client_port[FAKE_BUS].api = &k_fake_api;
    s_app.modbus.client_port[FAKE_BUS].impl = &s_fake;

    zassert_equal(composition_root_start(&s_app), 0, "Start failed");
    k_msleep(100);
    zassert_true(s_fake.calls > 0U, "The polling thread never reached the client");

    zassert_equal(composition_root_stop(&s_app), 0, "composition_root_stop failed");

    /* A stopped thread must be silent. */
    const uint32_t calls_after_stop = s_fake.calls;

    k_msleep(100);
    zassert_equal(s_fake.calls, calls_after_stop, "A stopped thread must not poll");

    /* Restarting must not inherit a stale STOP from the previous run. */
    zassert_equal(ao_modbus_start(&s_app.modbus.ao[0]), 0, "Restart failed");
    zassert_equal(ao_storage_start(&s_app.storage.ao), 0, "Restart ao_store failed");
    zassert_equal(ao_network_start(&s_app.network.ao), 0, "Restart ao_network failed");
    zassert_equal(ao_system_indicator_start(&s_app.indicators.ao), 0, "Restart ao_indicator failed");
    zassert_equal(ao_digital_input_start(&s_app.di.ao), 0, "Restart ao_di failed");

    k_msleep(1200); /* The default slave polls every 1000 ms. */

    zassert_true(s_fake.calls > calls_after_stop, "A restarted thread must poll again");

    zassert_equal(composition_root_stop(&s_app), 0, "composition_root_stop failed");
    (void) composition_root_deinit(&s_app);
}

ZTEST(composition_root_suite, test_digital_inputs_bound_and_queryable)
{
    (void) composition_root_deinit(&s_app);
    (void) memset(&s_app, 0, sizeof(s_app));

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");

    const i_digital_input_t *di = app_container_get_digital_inputs(&s_app);
    zassert_not_null(di, "Digital input port must not be NULL");
    zassert_true(i_digital_input_is_bound(di), "Digital input port must be bound");

    bool is_active = true;
    zassert_equal(i_digital_input_get_state(di, DI_ROLE_USER_BUTTON, &is_active), 0,
                  "Query DI_ROLE_USER_BUTTON state failed");
    zassert_false(is_active, "Initial button state must be inactive");

    zassert_equal(i_digital_input_get_state(di, DI_ROLE_BYPASS_SWITCH, &is_active), 0,
                  "Query DI_ROLE_BYPASS_SWITCH state failed");
    zassert_false(is_active, "Initial bypass switch state must be inactive");

    bool raw_level = true;
    zassert_equal(i_digital_input_get_raw_level(di, DI_ROLE_USER_BUTTON, &raw_level), 0,
                  "Query raw level failed");
    zassert_false(raw_level, "Initial raw level must be false");

    uint64_t mask = 0xFFFFFFFFULL;
    zassert_equal(i_digital_input_get_active_mask(di, &mask), 0,
                  "Query active mask failed");
    zassert_equal(mask, 0ULL, "Active mask must initially be 0");

    (void) composition_root_deinit(&s_app);
}

ZTEST(composition_root_suite, test_modular_subsystem_initialization_and_getters)
{
    app_container_t isolated_app;
    (void) memset(&isolated_app, 0, sizeof(isolated_app));

    /* Before any init, getters return NULL */
    zassert_is_null(app_container_get_registry(&isolated_app), "Must be NULL");
    zassert_is_null(app_container_get_modbus_client(&isolated_app, 0), "Must be NULL");
    zassert_is_null(app_container_get_relay(&isolated_app), "Must be NULL");
    zassert_is_null(app_container_get_storage(&isolated_app), "Must be NULL");
    zassert_is_null(app_container_get_network_eth(&isolated_app), "Must be NULL");
    zassert_is_null(app_container_get_network_wifi(&isolated_app), "Must be NULL");
    zassert_is_null(app_container_get_wifi_control(&isolated_app), "Must be NULL");
    zassert_is_null(app_container_get_digital_inputs(&isolated_app), "Must be NULL");
    zassert_is_null(app_container_get_led(&isolated_app), "Must be NULL");
    zassert_is_null(app_container_get_buzzer(&isolated_app), "Must be NULL");

    /* Out of bounds bus must return NULL */
    zassert_is_null(app_container_get_modbus_client(&isolated_app, MODBUS_BUS_MAX), "Out of bounds must be NULL");

    /* Initialize ONLY storage subsystem */
    int err = composition_root_init_storage(&isolated_app);
    zassert_equal(err, 0, "Storage subsystem should initialize independently");
    zassert_true(isolated_app.storage.initialized, "storage must be marked initialized");

    const i_config_storage_t *storage = app_container_get_storage(&isolated_app);
    zassert_not_null(storage, "Storage port must now be accessible");
    zassert_true(i_config_storage_is_bound(storage), "Storage port must be bound");

    /* Other subsystems remain uninitialized */
    zassert_is_null(app_container_get_registry(&isolated_app), "Modbus still uninitialized");
    zassert_is_null(app_container_get_network_eth(&isolated_app), "Network still uninitialized");

    /* Clean up isolated storage AO */
    (void) ao_storage_stop(&isolated_app.storage.ao);
    (void) ao_storage_wait_stopped(&isolated_app.storage.ao, K_MSEC(200));
    (void) ao_storage_deinit(&isolated_app.storage.ao);
}
