/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Composition-root smoke test: the container must wire the registry and start
 * one polling thread per bus, and the polling loop must reach the client port.
 * Runs on native_sim; no hardware and no nanoMODBUS.
 *
 * The Modbus topology is compile-time (app/Kconfig.modbus) and independent of
 * the HVAC profile, so every expectation below is derived from the compiled
 * table instead of from a seeded configuration.
 */

#include <zephyr/ztest.h>

#include <errno.h>
#include <string.h>

#include "infrastructure/di/composition_root.h"
#include "application/services/app_facade.h"
#include "fake_config_storage.h"
#include "application/zbus/digital_input_channels.h"
#include "common/config_types.h"
#include "common/config_envelope.h"
#include "common/modbus_device_table.h"

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
    .get_bus_cfg = fake_get_cfg,
};

/* The container is ~10 KB: keep it off the ztest stack. */
static app_container_t s_app;

/** Container used to prove Modbus no longer depends on the storage subsystem. */
static app_container_t s_bare_app;

static void composition_root_test_after(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)composition_root_deinit(&s_app);
    fake_config_storage_reset();
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
    fake_config_storage_reset();

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

    modbus_slave_snapshot_t snap_bus1[5];
    size_t count_bus1 = 0U;
    zassert_equal(i_slave_registry_snapshot(registry, MODBUS_BUS_RS485_1, snap_bus1, 5U, &count_bus1),
                  0, "Snapshot bus 1 failed");
    zassert_equal(count_bus1, 2U, "Bus 1 devices (JSY + CWT #1; CWT #2 is disabled)");

    modbus_slave_snapshot_t snap_bus2[5];
    size_t count_bus2 = 0U;
    zassert_equal(i_slave_registry_snapshot(registry, MODBUS_BUS_RS485_2, snap_bus2, 5U, &count_bus2),
                  0, "Snapshot bus 2 failed");
    zassert_equal(count_bus2, 2U, "Bus 2 devices (MBSL32DI + Waveshare relay)");

    /* Relay ports verification */
    const i_relay_t *relay_ext = app_container_get_relay(&s_app);
    zassert_not_null(relay_ext, "relay_ext must not be NULL");
    zassert_true(i_relay_is_bound(relay_ext), "relay_ext must be bound");
    zassert_true(i_relay_is_bound(&s_app.modbus.relay_int_port), "relay_int must be bound to local relay port");

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
    zassert_str_equal(s_app.network.ao.wifi_cfg.ap.ssid, CONFIG_BRIGHTC_WIFI_DEFAULT_SSID, "WiFi default SSID mismatch");

    /* Idempotent. */
    zassert_equal(composition_root_init(&s_app), 0, "Second init must be a no-op");
    (void) composition_root_deinit(&s_app);
}

ZTEST(composition_root_suite, test_start_runs_both_threads_and_polls_the_bound_bus)
{
    (void) composition_root_deinit(&s_app);
    (void) memset(&s_app, 0, sizeof(s_app));
    (void) memset(&s_fake, 0, sizeof(s_fake));
    fake_config_storage_reset();

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");

    /* Bind fake transport to both Bus 1 (Telemetry) and Bus 2 (Control) */
    s_app.modbus.client_port[MODBUS_BUS_RS485_1].api = &k_fake_api;
    s_app.modbus.client_port[MODBUS_BUS_RS485_1].impl = &s_fake;
    s_app.modbus.client_port[MODBUS_BUS_RS485_2].api = &k_fake_api;
    s_app.modbus.client_port[MODBUS_BUS_RS485_2].impl = &s_fake;

    const i_modbus_client_t *client1 = app_container_get_modbus_client(&s_app, MODBUS_BUS_RS485_1);
    zassert_not_null(client1, "Modbus client port 1 must not be NULL");
    zassert_true(i_modbus_client_is_bound(client1), "Modbus client port 1 must be bound");

    zassert_equal(composition_root_start(&s_app), 0, "Start failed");

    /* Let the polling threads run a few cycles. */
    k_msleep(200);

    zassert_true(s_fake.calls >= 4U, "The polling thread never reached the client");
    zassert_true(s_fake.calls_by_addr[MODBUS_ADDR_JSY_MK339] > 0U, "Slave JSY was never polled");
    zassert_true(s_fake.calls_by_addr[MODBUS_ADDR_CWT_TM16PT_1] > 0U, "Slave CWT #1 was never polled");
    zassert_true(s_fake.calls_by_addr[MODBUS_ADDR_MBSL32DI] > 0U, "Slave MBSL32DI was never polled");
    zassert_true(s_fake.calls_by_addr[MODBUS_ADDR_WAVESHARE_RELAY] > 0U, "Slave Waveshare was never polled");

    modbus_slave_snapshot_t snap[5];
    size_t count = 0U;

    const i_slave_registry_t *registry = app_container_get_registry(&s_app);
    zassert_not_null(registry, "Registry port must not be NULL");

    zassert_equal(i_slave_registry_snapshot(registry, MODBUS_BUS_RS485_1, snap, 5U, &count),
                  0, "Snapshot failed");
    zassert_equal(count, 2U, "Snapshot count mismatch on Bus 1");
    zassert_equal(snap[0].runtime.health, MODBUS_HEALTH_ONLINE, "A successful poll must be ONLINE");
    zassert_true(snap[0].runtime.successful_polls > 0U, "Success counter not updated");
    zassert_false(snap[0].runtime.in_flight, "The claim was not released");

    zassert_equal(snap[1].runtime.health, MODBUS_HEALTH_ONLINE, "A successful poll must be ONLINE");
    zassert_true(snap[1].runtime.successful_polls > 0U, "Success counter not updated");
    zassert_false(snap[1].runtime.in_flight, "The claim was not released");

    /* Failures must degrade the slave and reach the registry. */
    s_fake.next_result = MODBUS_RESULT_TIMEOUT;
    k_msleep(1200);

    zassert_equal(i_slave_registry_snapshot(registry, MODBUS_BUS_RS485_1, snap, 5U, &count),
                  0, "Snapshot failed");
    zassert_equal(count, 2U, "Snapshot count mismatch after failure");
    zassert_true(snap[0].runtime.timeout_count > 0U, "Timeouts not recorded");
    zassert_not_equal(snap[0].runtime.health, MODBUS_HEALTH_ONLINE, "Timeouts must degrade");
    zassert_true(snap[1].runtime.timeout_count > 0U, "Timeouts not recorded");
    zassert_not_equal(snap[1].runtime.health, MODBUS_HEALTH_ONLINE, "Timeouts must degrade");

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
    zassert_equal(i_digital_input_get_state(di, DI_ROLE_SMOKE_FIRE, &is_active), 0,
                  "Query DI_ROLE_SMOKE_FIRE state failed");
    zassert_false(is_active, "Initial smoke/fire state must be inactive");

    zassert_equal(i_digital_input_get_state(di, DI_ROLE_DOOR_OPEN, &is_active), 0,
                  "Query DI_ROLE_DOOR_OPEN state failed");
    zassert_false(is_active, "Initial door open state must be inactive");

    bool raw_level = true;
    zassert_equal(i_digital_input_get_raw_level(di, DI_ROLE_SMOKE_FIRE, &raw_level), 0,
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

    /* Calling the persisted-configuration dependents before storage must fail
     * with -EAGAIN. Modbus is intentionally absent from this list: its topology
     * is compile-time and its line parameters come from the devicetree, so it
     * has no boot dependency on the storage subsystem (see the dedicated test
     * below). */
    zassert_equal(composition_root_init_network(&isolated_app), -EAGAIN, "Network before storage must return -EAGAIN");
    zassert_equal(composition_root_init_hvac(&isolated_app), -EAGAIN, "HVAC before storage must return -EAGAIN");

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

    /* Modbus must come up on a container that has no storage at all: nothing is
     * loaded from EEPROM and nothing is persisted, so the old -EAGAIN gate is
     * gone by design. */
    (void) memset(&s_bare_app, 0, sizeof(s_bare_app));

    zassert_equal(composition_root_init_modbus(&s_bare_app), 0,
                  "Modbus must initialize without the storage subsystem");
    zassert_true(s_bare_app.modbus.initialized, "modbus must be marked initialized");
    zassert_true(i_slave_registry_is_bound(&s_bare_app.modbus.registry_port),
                 "The registry port must be bound without storage");

    /* Let the generic teardown stop the polling threads created above. */
    s_bare_app.is_initialized = true;
    (void) composition_root_deinit(&s_bare_app);
}

ZTEST(composition_root_suite, test_modbus_static_topology_initialization)
{
    (void)composition_root_deinit(&s_app);
    (void)memset(&s_app, 0, sizeof(s_app));
    fake_config_storage_reset();

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");

    /* The compiled table is the contract: every enabled row must be registered
     * with exactly the compiled bus and unit address, and every row disabled at
     * build time must be absent. */
    size_t table_count = 0U;
    const modbus_slave_descriptor_t *table = modbus_device_table_get_slaves(&table_count);

    zassert_equal(table_count, MODBUS_STANDARD_SLAVES_COUNT, "Compiled table size mismatch");

    size_t enabled_bus1 = 0U;
    size_t enabled_bus2 = 0U;

    for (size_t i = 0U; i < table_count; i++) {
        modbus_slave_descriptor_t found;

        int rc = modbus_registry_get(&s_app.modbus.registry_impl, table[i].device_id, &found);

        if (!table[i].is_enabled) {
            zassert_equal(rc, -ENOENT,
                          "Device %u is disabled at build time and must not be registered",
                          table[i].device_id);
            continue;
        }

        zassert_equal(rc, 0, "Device %u is enabled at build time and must be registered",
                      table[i].device_id);
        zassert_equal(found.slave_addr, table[i].slave_addr, "Device %u address mismatch",
                      table[i].device_id);
        zassert_equal(found.bus_id, table[i].bus_id, "Device %u bus mismatch", table[i].device_id);
        zassert_equal(found.device_id, found.slave_addr,
                      "The device id must equal the unit address");

        if (table[i].bus_id == (uint8_t)MODBUS_BUS_RS485_1) {
            enabled_bus1++;
        } else {
            enabled_bus2++;
        }
    }

    /* Kconfig, not EEPROM, decides the addresses. */
    const i_slave_registry_t *registry = app_container_get_registry(&s_app);
    zassert_not_null(registry, "Registry port must be bound");
    zassert_true(i_slave_registry_is_bound(registry), "Registry port must be bound");

    modbus_slave_snapshot_t snap[MODBUS_MAX_SLAVES_PER_BUS];
    size_t count = 0U;

    zassert_equal(i_slave_registry_snapshot(registry, MODBUS_BUS_RS485_1, snap,
                                            MODBUS_MAX_SLAVES_PER_BUS, &count),
                  0, "Snapshot bus 1 failed");
    zassert_equal(count, enabled_bus1, "Bus 1 must expose exactly the enabled slaves");

    zassert_equal(i_slave_registry_snapshot(registry, MODBUS_BUS_RS485_2, snap,
                                            MODBUS_MAX_SLAVES_PER_BUS, &count),
                  0, "Snapshot bus 2 failed");
    zassert_equal(count, enabled_bus2, "Bus 2 must expose exactly the enabled slaves");

    modbus_slave_descriptor_t desc;

    zassert_equal(modbus_registry_find_by_type(&s_app.modbus.registry_impl,
                                               MODBUS_DEV_TYPE_JSY_MK339, &desc),
                  0, "JSY meter must exist");
    zassert_equal(desc.slave_addr, MODBUS_ADDR_JSY_MK339, "JSY address");

    zassert_equal(modbus_registry_find_by_type(&s_app.modbus.registry_impl,
                                               MODBUS_DEV_TYPE_CWT_TM16PT, &desc),
                  0, "CWT concentrator #1 must exist");
    zassert_equal(desc.slave_addr, MODBUS_ADDR_CWT_TM16PT_1, "CWT #1 address");

    if (MODBUS_ENABLE_MBSL32DI) {
        zassert_equal(modbus_registry_find_by_type(&s_app.modbus.registry_impl,
                                                   MODBUS_DEV_TYPE_MBSL32DI, &desc),
                      0, "MBSL32DI is enabled at build time and must exist");
        zassert_equal(desc.slave_addr, MODBUS_ADDR_MBSL32DI, "MBSL32DI address");
    }

    if (MODBUS_ENABLE_WAVESHARE_RELAY) {
        zassert_equal(modbus_registry_find_by_type(&s_app.modbus.registry_impl,
                                                   MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH, &desc),
                      0, "Waveshare relay is enabled at build time and must exist");
        zassert_equal(desc.slave_addr, MODBUS_ADDR_WAVESHARE_RELAY, "Waveshare relay address");
        zassert_equal(s_app.modbus.waveshare_relay.slave_addr, MODBUS_ADDR_WAVESHARE_RELAY,
                      "Relay adapter must target the compiled address");
        zassert_true(i_relay_is_bound(&s_app.modbus.relay_ext_port),
                     "External relay port must be bound");
    } else {
        zassert_false(i_relay_is_bound(&s_app.modbus.relay_ext_port),
                      "External relay port must stay unbound");
    }

    (void)composition_root_deinit(&s_app);

    /* The HVAC profile is not part of the Modbus topology: seeding the most
     * different profile available must not change a single registered slave. */
    (void)memset(&s_app, 0, sizeof(s_app));
    fake_config_storage_reset();

    uint8_t hvac_buf[sizeof(config_envelope_header_t) + sizeof(config_hvac_t)];
    size_t packed_len = 0U;
    const config_hvac_t *telem_cfg = default_config_get_hvac_profile(HVAC_MODE_TELEMETRY_ONLY);
    zassert_equal(config_envelope_pack(CONFIG_DOMAIN_HVAC, telem_cfg, sizeof(*telem_cfg),
                                       hvac_buf, sizeof(hvac_buf), &packed_len), 0);
    zassert_equal(fake_config_storage_seed_domain(CONFIG_DOMAIN_HVAC, hvac_buf, packed_len), 0);

    zassert_equal(composition_root_init(&s_app), 0, "Init with Telemetry-Only failed");

    for (size_t i = 0U; i < table_count; i++) {
        modbus_slave_descriptor_t found;
        int rc = modbus_registry_get(&s_app.modbus.registry_impl, table[i].device_id, &found);

        zassert_equal(rc, table[i].is_enabled ? 0 : -ENOENT,
                      "Device %u presence must not depend on the HVAC profile",
                      table[i].device_id);
    }

    (void)composition_root_deinit(&s_app);
}

ZTEST(composition_root_suite, test_all_config_domains_registered_and_persistable)
{
    /* Verify every remaining configuration domain has a registered provider and
     * can persist. The Modbus topology no longer has a domain: it is compiled
     * in, which is why only CONFIG_DOMAIN_MAX domains are exercised here. */
    (void)composition_root_deinit(&s_app);
    (void)memset(&s_app, 0, sizeof(s_app));
    fake_config_storage_reset();

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");
    zassert_equal(composition_root_start(&s_app), 0, "Start failed");

    for (int d = (int)CONFIG_DOMAIN_DEVICE; d < (int)CONFIG_DOMAIN_MAX; d++)
    {
        int rc = ao_storage_mark_dirty(&s_app.storage.ao, (config_domain_t)d, 0U);
        zassert_equal(rc, 0, "Marking domain %d dirty must succeed", d);
    }

    zassert_equal(composition_root_stop(&s_app), 0, "Stop failed");

    const fake_config_storage_state_t *st = fake_config_storage_get_state();
    for (int d = (int)CONFIG_DOMAIN_DEVICE; d < (int)CONFIG_DOMAIN_MAX; d++)
    {
        zassert_true(st->slots[d].has_data, "Domain %d must be persisted", d);
    }

    (void)composition_root_deinit(&s_app);
}

ZTEST(composition_root_suite, test_app_facade_sensor_get_matrix_and_snapshot_zbus)
{
    (void)composition_root_deinit(&s_app);
    (void)memset(&s_app, 0, sizeof(s_app));
    fake_config_storage_reset();

    /* Before facade init or when uninitialized, app_facade_sensor_get_matrix returns -ENODEV */
    thermal_matrix_frame_t frame;
    sensor_snapshot_msg_t snap;
    zassert_equal(app_facade_sensor_get_matrix(&frame), -ENODEV, "Uninitialized facade returns -ENODEV");
    zassert_equal(app_facade_sensor_get_matrix(NULL), -EINVAL, "NULL frame returns -EINVAL");
    zassert_equal(app_facade_sensor_get_snapshot(&snap), -ENODEV, "Uninitialized facade returns -ENODEV");
    zassert_equal(app_facade_sensor_get_snapshot(NULL), -EINVAL, "NULL snapshot returns -EINVAL");

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");
    zassert_equal(app_facade_init(&s_app), 0, "Facade init failed");

    /* When sensor matrix is initialized, app_facade_sensor_get_matrix retrieves latest frame */
    zassert_equal(app_facade_sensor_get_matrix(&frame), 0, "Get matrix succeeded");

    /* Test flat snapshot reading from Zbus via facade */
    sensor_snapshot_msg_t snap_pub = {
        .valid_mask = (1ULL << 0) | (1ULL << 9), /* RTD0 and CWT1 CH0 */
        .temperatures = { [0] = 21.0f, [9] = 25.5f },
        .sequence = 42U,
        .timestamp_ms = 1000ULL,
    };
    zassert_equal(zbus_chan_pub(&chan_sensor_snapshot, &snap_pub, K_NO_WAIT), 0);

    sensor_snapshot_msg_t snap_rx;
    zassert_equal(app_facade_sensor_get_snapshot(&snap_rx), 0);
    zassert_equal(snap_rx.sequence, 42U);
    zassert_within(snap_rx.temperatures[9], 25.5f, 0.01f);

    app_facade_sensor_view_t view;
    zassert_equal(app_facade_sensor_get_readings(&view), 0);
    zassert_within(view.internal_rtd_temps[0], 21.0f, 0.01f);
    zassert_true(view.internal_rtd_valid[0]);
    zassert_within(view.modbus_rtd_temps[0], 25.5f, 0.01f);
    zassert_true(view.modbus_rtd_valid[0]);
    zassert_false(view.modbus_rtd_valid[3]);

    (void)composition_root_deinit(&s_app);
}

ZTEST(composition_root_suite, test_app_facade_hvac_configure_group_sensors_and_units)
{
    (void)composition_root_deinit(&s_app);
    (void)memset(&s_app, 0, sizeof(s_app));
    fake_config_storage_reset();

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");
    zassert_equal(app_facade_init(&s_app), 0, "Facade init failed");

    /* Parameter validation checks */
    zassert_equal(app_facade_hvac_configure_group_sensors(HVAC_MAX_GROUPS, 1, 4, SENSOR_METRIC_MAX), -EINVAL);
    zassert_equal(app_facade_hvac_configure_group_sensors(0, 5, 2, SENSOR_METRIC_MAX), -EINVAL);
    zassert_equal(app_facade_hvac_configure_group_sensors(0, 1, 4, (sensor_metric_policy_t)99), -EINVAL);

    zassert_equal(app_facade_hvac_configure_group_units(HVAC_MAX_GROUPS, 2, 0x0F), -EINVAL);
    zassert_equal(app_facade_hvac_configure_group_units(0, 99, 0x0F), -EINVAL);

    /* Valid configuration */
    zassert_equal(app_facade_hvac_configure_group_sensors(0, 2, 5, SENSOR_METRIC_AVG), 0);
    zassert_equal(app_facade_hvac_configure_group_units(0, 2, 0x0F), 0);

    /* Verify via status query */
    app_facade_hvac_status_t status;
    zassert_equal(app_facade_hvac_get_status(&status), 0);
    zassert_equal(status.groups[0].sensor_start, 2U);
    zassert_equal(status.groups[0].sensor_end, 5U);
    zassert_equal(status.groups[0].metric_policy, (uint8_t)SENSOR_METRIC_AVG);
    zassert_equal(status.groups[0].active_turn_count, 2U);
    zassert_equal(status.groups[0].unit_mask, 0x0FU);

    (void)composition_root_deinit(&s_app);
}

ZTEST(composition_root_suite, test_composition_root_di_feed_and_hvac_safety_interlock)
{
    (void)composition_root_deinit(&s_app);
    (void)memset(&s_app, 0, sizeof(s_app));
    fake_config_storage_reset();

    zassert_equal(composition_root_init(&s_app), 0, "Init failed");
    zassert_equal(app_facade_init(&s_app), 0, "Facade init failed");

    /* Initial state: no fire alarm */
    zassert_false(s_app.hvac.ao.is_fire_active);

    /* Publish DI snapshot with SMOKE_FIRE asserted and UNIT_0_ALARM_HP asserted */
    di_snapshot_msg_t snap = {
        .channel_count = 2U,
        .roles = { DI_ROLE_SMOKE_FIRE, DI_ROLE_UNIT_0_ALARM_HP },
        .is_active = { true, true },
        .active_mask = 0x03ULL,
        .raw_mask = 0x03ULL,
        .sequence = 10U,
        .timestamp_ms = 5000ULL,
    };
    zassert_equal(zbus_chan_pub(&chan_digital_input_snapshot, &snap, K_NO_WAIT), 0);

    /* Verify listener updated HVAC fire safety state synchronously */
    zassert_true(s_app.hvac.ao.is_fire_active, "Fire alarm must be asserted in HVAC AO");
    zassert_true(s_app.hvac.ao.units[0].high_pressure_alarm, "Unit 0 HP alarm must be asserted");
    zassert_false(s_app.hvac.ao.units[1].high_pressure_alarm, "Unit 1 HP alarm must remain clear");

    /* Verify facade query retrieves snapshot accurately */
    app_facade_di_view_t view;
    zassert_equal(app_facade_di_get_status(&view), 0);
    zassert_equal(view.channel_count, 2U);
    zassert_true(view.channels[0].is_active);
    zassert_equal(view.channels[0].role, DI_ROLE_SMOKE_FIRE);

    di_snapshot_msg_t snap_rx;
    zassert_equal(app_facade_di_get_snapshot(&snap_rx), 0);
    zassert_equal(snap_rx.sequence, 10U);

    /* Clear fire alarm */
    snap.is_active[0] = false;
    snap.active_mask = 0x02ULL;
    zassert_equal(zbus_chan_pub(&chan_digital_input_snapshot, &snap, K_NO_WAIT), 0);
    zassert_false(s_app.hvac.ao.is_fire_active, "Fire alarm must be cleared in HVAC AO");

    (void)composition_root_deinit(&s_app);
}

ZTEST(composition_root_suite, test_app_facade_thermal_queries)
{
    (void)composition_root_deinit(&s_app);
    (void)memset(&s_app, 0, sizeof(s_app));
    fake_config_storage_reset();

    /* Uninitialized defensive checks */
    thermal_energy_result_t res;
    thermal_energy_capture_t cap;
    thermal_hourly_record_t hist;
    config_thermal_baseline_t base;
    config_thermal_site_t site;

    zassert_equal(app_facade_thermal_get_result(NULL), -EINVAL);
    zassert_equal(app_facade_thermal_get_capture(NULL), -EINVAL);
    zassert_equal(app_facade_thermal_get_hourly_history(0, NULL), -EINVAL);
    zassert_equal(app_facade_thermal_get_baseline(NULL), -EINVAL);
    zassert_equal(app_facade_thermal_get_site_config(NULL), -EINVAL);
    zassert_equal(app_facade_thermal_trigger_recalibration(), -EINVAL);

    /* Initialize composition root and facade */
    zassert_equal(composition_root_init(&s_app), 0, "Composition root init failed");
    zassert_equal(app_facade_init(&s_app), 0, "Facade init failed");

    /* Test valid queries */
    zassert_equal(app_facade_thermal_get_result(&res), 0, "Thermal get result failed");
    zassert_equal(app_facade_thermal_get_capture(&cap), 0, "Thermal get capture failed");
    zassert_equal(app_facade_thermal_get_hourly_history(0, &hist), -ENOENT, "Empty history must return -ENOENT");
    zassert_equal(app_facade_thermal_get_baseline(&base), 0, "Thermal get baseline failed");
    zassert_equal(app_facade_thermal_get_site_config(&site), 0, "Thermal get site config failed");
    zassert_true(site.configured_pairs_count > 0U, "Site pairs count must be > 0");

    /* Trigger recalibration */
    zassert_equal(app_facade_thermal_trigger_recalibration(), 0, "Trigger recalibration failed");

    (void)composition_root_deinit(&s_app);
}

