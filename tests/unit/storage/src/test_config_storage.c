/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <errno.h>

#include "common/config_types.h"
#include "common/config_envelope.h"
#include "common/thermal_energy_types.h"
#include "common/app_defaults.h"
#include "interfaces/i_config_storage.h"
#include "fake_config_storage.h"
#include "application/zbus/config_channels.h"
#include "application/active_objects/ao_storage.h"

ZTEST_SUITE(storage_suite, NULL, NULL, NULL, NULL, NULL);




ZTEST(storage_suite, test_config_domain_enum_and_validation)
{
    zassert_equal(CONFIG_DOMAIN_DEVICE, 0, "CONFIG_DOMAIN_DEVICE should be 0");
    zassert_equal(CONFIG_DOMAIN_NET_ETH, 1, "CONFIG_DOMAIN_NET_ETH should be 1");
    zassert_equal(CONFIG_DOMAIN_NET_WIFI, 2, "CONFIG_DOMAIN_NET_WIFI should be 2");
    zassert_equal(CONFIG_DOMAIN_HVAC, 3, "CONFIG_DOMAIN_HVAC should be 3");
    zassert_equal(CONFIG_DOMAIN_CALIBRATION, 4, "CONFIG_DOMAIN_CALIBRATION should be 4");
    zassert_equal(CONFIG_DOMAIN_THERMAL_SITE, 5, "CONFIG_DOMAIN_THERMAL_SITE should be 5");
    zassert_equal(CONFIG_DOMAIN_THERMAL_BASELINE, 6, "CONFIG_DOMAIN_THERMAL_BASELINE should be 6");
    zassert_equal(CONFIG_DOMAIN_MAX, 7, "CONFIG_DOMAIN_MAX should be 7");

    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_DEVICE), "DEVICE must be valid");
    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_NET_ETH), "NET_ETH must be valid");
    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_NET_WIFI), "NET_WIFI must be valid");
    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_HVAC), "HVAC must be valid");
    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_CALIBRATION), "CALIBRATION must be valid");
    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_THERMAL_SITE), "THERMAL_SITE must be valid");
    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_THERMAL_BASELINE), "THERMAL_BASELINE must be valid");

    zassert_false(config_domain_is_valid(CONFIG_DOMAIN_MAX), "MAX must be invalid");
    zassert_false(config_domain_is_valid((config_domain_t)-1), "-1 must be invalid");
    zassert_false(config_domain_is_valid((config_domain_t)99), "99 must be invalid");
}

ZTEST(storage_suite, test_config_event_type_validation)
{
    zassert_equal(CONFIG_EVENT_DIRTY, 0, "CONFIG_EVENT_DIRTY should be 0");
    zassert_equal(CONFIG_EVENT_PERSISTED, 1, "CONFIG_EVENT_PERSISTED should be 1");
    zassert_equal(CONFIG_EVENT_ERROR, 2, "CONFIG_EVENT_ERROR should be 2");

    zassert_true(config_event_type_is_valid(CONFIG_EVENT_DIRTY), "DIRTY must be valid");
    zassert_true(config_event_type_is_valid(CONFIG_EVENT_PERSISTED), "PERSISTED must be valid");
    zassert_true(config_event_type_is_valid(CONFIG_EVENT_ERROR), "ERROR must be valid");
    zassert_false(config_event_type_is_valid((config_event_type_t)3), "3 must be invalid");
    zassert_false(config_event_type_is_valid((config_event_type_t)-1), "-1 must be invalid");
}

ZTEST(storage_suite, test_config_domain_layout)
{
    size_t base = 0;
    size_t total = 0;
    size_t slot = 0;

    /* DEVICE: 0x0000, 256, 128 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_DEVICE, &base, &total, &slot), 0, "DEVICE layout error");
    zassert_equal(base, 0x0000, "DEVICE base");
    zassert_equal(total, 256, "DEVICE total");
    zassert_equal(slot, 128, "DEVICE slot");

    /* NET_ETH: 0x0100, 128, 64 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_NET_ETH, &base, &total, &slot), 0, "NET_ETH layout error");
    zassert_equal(base, 0x0100, "NET_ETH base");
    zassert_equal(total, 128, "NET_ETH total");
    zassert_equal(slot, 64, "NET_ETH slot");

    /* NET_WIFI: 0x0180, 640, 320 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_NET_WIFI, &base, &total, &slot), 0, "NET_WIFI layout error");
    zassert_equal(base, 0x0180, "NET_WIFI base");
    zassert_equal(total, 640, "NET_WIFI total");
    zassert_equal(slot, 320, "NET_WIFI slot");

    /* HVAC: 0x0400, 1152, 576 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_HVAC, &base, &total, &slot), 0, "HVAC layout error");
    zassert_equal(base, 0x0400, "HVAC base");
    zassert_equal(total, 1152, "HVAC total");
    zassert_equal(slot, 576, "HVAC slot");

    /* CALIBRATION: 0x0880, 384, 192 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_CALIBRATION, &base, &total, &slot), 0, "CALIBRATION layout error");
    zassert_equal(base, 0x0880, "CALIBRATION base");
    zassert_equal(total, 384, "CALIBRATION total");
    zassert_equal(slot, 192, "CALIBRATION slot");

    /* THERMAL_SITE: 0x0A00, 1536, 768 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_THERMAL_SITE, &base, &total, &slot), 0, "THERMAL_SITE layout error");
    zassert_equal(base, 0x0A00, "THERMAL_SITE base");
    zassert_equal(total, 1536, "THERMAL_SITE total");
    zassert_equal(slot, 768, "THERMAL_SITE slot");

    /* THERMAL_BASELINE: 0x1000, 1024, 512 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_THERMAL_BASELINE, &base, &total, &slot), 0, "THERMAL_BASELINE layout error");
    zassert_equal(base, 0x1000, "THERMAL_BASELINE base");
    zassert_equal(total, 1024, "THERMAL_BASELINE total");
    zassert_equal(slot, 512, "THERMAL_BASELINE slot");

    /* Invalid domain */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_MAX, &base, &total, &slot), -EINVAL, "MAX invalid");
    zassert_equal(config_domain_get_layout((config_domain_t)99, &base, &total, &slot), -EINVAL, "99 invalid");
}

ZTEST(storage_suite, test_i_config_storage_wrappers_unbound_and_null)
{
    uint8_t buf[32];
    size_t out_sz = 0;

    zassert_false(i_config_storage_is_bound(NULL), "NULL storage handle is not bound");

    i_config_storage_t empty = {0};
    zassert_false(i_config_storage_is_bound(&empty), "Empty storage handle is not bound");

    /* Calls with NULL */
    zassert_equal(i_config_storage_init(NULL), -EINVAL, "init(NULL)");
    zassert_equal(i_config_storage_save(NULL, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf)), -EINVAL, "save(NULL)");
    zassert_equal(i_config_storage_load(NULL, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf), &out_sz), -EINVAL, "load(NULL)");
    zassert_equal(i_config_storage_reset_defaults(NULL, CONFIG_DOMAIN_DEVICE), -EINVAL, "reset_defaults(NULL)");

    /* Calls with unbound struct */
    zassert_equal(i_config_storage_init(&empty), -EINVAL, "init(empty)");
    zassert_equal(i_config_storage_save(&empty, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf)), -EINVAL, "save(empty)");
    zassert_equal(i_config_storage_load(&empty, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf), &out_sz), -EINVAL, "load(empty)");
    zassert_equal(i_config_storage_reset_defaults(&empty, CONFIG_DOMAIN_DEVICE), -EINVAL, "reset_defaults(empty)");
}

ZTEST(storage_suite, test_fake_config_storage_operations)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();
    zassert_true(i_config_storage_is_bound(&port), "Fake storage must be bound");

    /* 1. Init */
    zassert_equal(i_config_storage_init(&port), 0, "Init failed");
    const fake_config_storage_state_t *state = fake_config_storage_get_state();
    zassert_equal(state->init_calls, 1, "Init call counter mismatch");

    /* 2. Load non-existent data */
    uint8_t read_buf[64] = {0};
    size_t out_size = 0;
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_NET_ETH, read_buf, sizeof(read_buf), &out_size), -ENOENT, "Should return -ENOENT for empty slot");

    /* 3. Save data */
    uint8_t dummy_payload[] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22};
    zassert_equal(i_config_storage_save(&port, CONFIG_DOMAIN_NET_ETH, dummy_payload, sizeof(dummy_payload)), 0, "Save failed");
    zassert_equal(state->save_calls, 1, "Save call counter mismatch");
    zassert_equal(state->slots[CONFIG_DOMAIN_NET_ETH].save_count, 1, "Slot save count mismatch");

    /* 4. Load saved data */
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_NET_ETH, read_buf, sizeof(read_buf), &out_size), 0, "Load failed");
    zassert_equal(out_size, sizeof(dummy_payload), "Size mismatch");
    zassert_mem_equal(read_buf, dummy_payload, sizeof(dummy_payload), "Content mismatch");

    /* Buffer too small */
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_NET_ETH, read_buf, 2, &out_size), -ENOSPC, "Should return -ENOSPC");

    /* 5. Domain isolation check: another domain is still empty */
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_NET_WIFI, read_buf, sizeof(read_buf), &out_size), -ENOENT, "WIFI should still be empty");

    /* 6. Reset defaults */
    zassert_equal(i_config_storage_reset_defaults(&port, CONFIG_DOMAIN_NET_ETH), 0, "Reset failed");
    zassert_equal(state->reset_defaults_calls, 1, "Reset call counter mismatch");
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_NET_ETH, read_buf, sizeof(read_buf), &out_size), -ENOENT, "Should be empty after reset");
}

ZTEST(storage_suite, test_fake_config_storage_error_simulation)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();

    fake_config_storage_set_errors(-EIO, -ENOMEM, -EBUSY, -EFAULT);
    uint8_t buf[16] = {0};
    size_t out_sz = 0;

    zassert_equal(i_config_storage_init(&port), -EIO, "Expected init error");
    zassert_equal(i_config_storage_save(&port, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf)), -ENOMEM, "Expected save error");
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf), &out_sz), -EBUSY, "Expected load error");
    zassert_equal(i_config_storage_reset_defaults(&port, CONFIG_DOMAIN_DEVICE), -EFAULT, "Expected reset error");

    /* Clear errors */
    fake_config_storage_set_errors(0, 0, 0, 0);
    zassert_equal(i_config_storage_init(&port), 0, "Init should now succeed");
}

/* File-scope stack and queue for ao_storage tests */
K_THREAD_STACK_DEFINE(s_test_storage_stack, AO_STORAGE_STACK_SIZE);
K_MSGQ_DEFINE(s_test_storage_queue, sizeof(ao_storage_event_t), AO_STORAGE_QUEUE_DEPTH, 4);
static ao_storage_t s_test_ao_storage;

/* Dummy test provider */
static uint8_t s_mock_net_eth_data[16] = {1, 2, 3, 4, 5, 6, 7, 8};
static size_t s_mock_net_eth_size = 8;
static int s_mock_provider_err = 0;

static int test_net_eth_provider(void *ctx, config_domain_t domain, void *out_buf, size_t max_size, size_t *out_size)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(domain);
    if (s_mock_provider_err != 0) {
        return s_mock_provider_err;
    }
    if (max_size < s_mock_net_eth_size) {
        return -ENOSPC;
    }
    memcpy(out_buf, s_mock_net_eth_data, s_mock_net_eth_size);
    if (out_size != NULL) {
        *out_size = s_mock_net_eth_size;
    }
    return 0;
}

ZTEST(storage_suite, test_ao_storage_init_validation)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();

    /* Invalid arguments */
    zassert_equal(ao_storage_init(NULL, &port, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), -EINVAL, "NULL self");
    zassert_equal(ao_storage_init(&s_test_ao_storage, NULL, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), -EINVAL, "NULL storage");
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, NULL, s_test_storage_stack, AO_STORAGE_STACK_SIZE), -EINVAL, "NULL queue");
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, NULL, AO_STORAGE_STACK_SIZE), -EINVAL, "NULL stack");
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, s_test_storage_stack, 0), -EINVAL, "0 stack size");

    i_config_storage_t unbound = {0};
    zassert_equal(ao_storage_init(&s_test_ao_storage, &unbound, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), 0, "Unbound storage initializes in degraded mode");
    zassert_true(ao_storage_is_degraded(&s_test_ao_storage), "Storage AO should be degraded");
    zassert_equal(ao_storage_deinit(&s_test_ao_storage), 0, "Deinit failed");

    /* Valid initialization with bound port */
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), 0, "Valid init failed");
    zassert_false(ao_storage_is_degraded(&s_test_ao_storage), "Storage AO should not be degraded");
    zassert_equal(ao_storage_deinit(&s_test_ao_storage), 0, "Deinit failed");
}

ZTEST(storage_suite, test_ao_storage_provider_registration)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), 0, "Init");

    /* Invalid provider registration */
    zassert_equal(ao_storage_register_provider(NULL, CONFIG_DOMAIN_NET_ETH, test_net_eth_provider, NULL), -EINVAL, "NULL self");
    zassert_equal(ao_storage_register_provider(&s_test_ao_storage, CONFIG_DOMAIN_MAX, test_net_eth_provider, NULL), -EINVAL, "Invalid domain");
    zassert_equal(ao_storage_register_provider(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, NULL, NULL), -EINVAL, "NULL fn");

    /* Valid registration */
    zassert_equal(ao_storage_register_provider(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, test_net_eth_provider, NULL), 0, "Valid registration");
    zassert_equal(ao_storage_deinit(&s_test_ao_storage), 0, "Deinit");
}

ZTEST(storage_suite, test_ao_storage_sync_flush_and_error_handling)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), 0, "Init");

    s_mock_provider_err = 0;
    zassert_equal(ao_storage_register_provider(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, test_net_eth_provider, NULL), 0, "Register");

    /* Mark domain dirty without running thread */
    s_test_ao_storage.dirty_mask |= (1U << CONFIG_DOMAIN_NET_ETH);
    int flushed = ao_storage_sync_flush(&s_test_ao_storage);
    zassert_equal(flushed, 1, "Should have flushed 1 domain");
    zassert_equal(s_test_ao_storage.dirty_mask, 0, "Dirty mask should be cleared");

    const fake_config_storage_state_t *st = fake_config_storage_get_state();
    zassert_equal(st->slots[CONFIG_DOMAIN_NET_ETH].save_count, 1, "Save count should be 1");
    zassert_mem_equal(st->slots[CONFIG_DOMAIN_NET_ETH].data, s_mock_net_eth_data, s_mock_net_eth_size, "Saved data mismatch");

    /* Test error in provider */
    s_mock_provider_err = -EIO;
    s_test_ao_storage.dirty_mask |= (1U << CONFIG_DOMAIN_NET_ETH);
    flushed = ao_storage_sync_flush(&s_test_ao_storage);
    zassert_equal(flushed, 0, "Should have failed to persist");
    zassert_equal(s_test_ao_storage.dirty_mask, 0, "Dirty mask should still be cleared on handled error");

    /* Test error in storage save */
    s_mock_provider_err = 0;
    fake_config_storage_set_errors(0, -EIO, 0, 0);
    s_test_ao_storage.dirty_mask |= (1U << CONFIG_DOMAIN_NET_ETH);
    flushed = ao_storage_sync_flush(&s_test_ao_storage);
    zassert_equal(flushed, 0, "Should have failed to persist on storage error");
    fake_config_storage_set_errors(0, 0, 0, 0);

    zassert_equal(ao_storage_deinit(&s_test_ao_storage), 0, "Deinit");
}

ZTEST(storage_suite, test_ao_storage_lifecycle_and_debounce)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), 0, "Init");

    s_mock_provider_err = 0;
    zassert_equal(ao_storage_register_provider(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, test_net_eth_provider, NULL), 0, "Register");

    /* Start thread */
    zassert_equal(ao_storage_start(&s_test_ao_storage), 0, "Start");
    zassert_equal(ao_storage_start(&s_test_ao_storage), -EALREADY, "Double start should return -EALREADY");

    /* Mark dirty */
    zassert_equal(ao_storage_mark_dirty(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, 0), 0, "Mark dirty");

    /* Check immediately: should NOT be written yet due to 500 ms debounce */
    const fake_config_storage_state_t *st = fake_config_storage_get_state();
    zassert_equal(st->slots[CONFIG_DOMAIN_NET_ETH].save_count, 0, "Should not be persisted immediately (debounce)");

    /* Immediate flush command */
    zassert_equal(ao_storage_flush_now(&s_test_ao_storage), 0, "Flush now");
    k_sleep(K_MSEC(50));
    zassert_equal(st->slots[CONFIG_DOMAIN_NET_ETH].save_count, 1, "Should be persisted after flush_now");

    /* Mark dirty again and let debounce timer expire */
    zassert_equal(ao_storage_mark_dirty(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, 0), 0, "Mark dirty again");
    /* Wait for debounce (500 ms + margin) */
    k_sleep(K_MSEC(600));
    zassert_equal(st->slots[CONFIG_DOMAIN_NET_ETH].save_count, 2, "Should be persisted after debounce window");

    /* Stop Active Object */
    zassert_equal(ao_storage_stop(&s_test_ao_storage), 0, "Stop");
    zassert_equal(ao_storage_wait_stopped(&s_test_ao_storage, K_MSEC(2000)), 0, "Wait stopped");
    zassert_equal(ao_storage_stop(&s_test_ao_storage), 0, "Stop on already stopped AO should succeed");

    /* Deinit */
    zassert_equal(ao_storage_deinit(&s_test_ao_storage), 0, "Deinit");
}

ZTEST(storage_suite, test_config_envelope_packing_and_validation)
{
    uint8_t payload[32];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 3 + 7);
    }

    uint8_t env_buf[64];
    size_t env_len = 0;

    /* Pack envelope */
    int ret = config_envelope_pack(CONFIG_DOMAIN_NET_ETH, payload, sizeof(payload),
                                   env_buf, sizeof(env_buf), &env_len);
    zassert_equal(ret, 0, "Pack envelope failed");
    zassert_equal(env_len, CONFIG_ENVELOPE_HEADER_SIZE + sizeof(payload), "Envelope length mismatch");

    /* Unpack envelope */
    uint8_t unpacked[32];
    size_t unpacked_len = 0;
    ret = config_envelope_unpack(CONFIG_DOMAIN_NET_ETH, env_buf, env_len,
                                 unpacked, sizeof(unpacked), &unpacked_len);
    zassert_equal(ret, 0, "Unpack envelope failed");
    zassert_equal(unpacked_len, sizeof(payload), "Unpacked length mismatch");
    zassert_mem_equal(unpacked, payload, sizeof(payload), "Payload mismatch");

    /* Domain mismatch detection */
    ret = config_envelope_unpack(CONFIG_DOMAIN_HVAC, env_buf, env_len,
                                 unpacked, sizeof(unpacked), &unpacked_len);
    zassert_equal(ret, -EBADMSG, "Should reject domain mismatch");

    /* Buffer too small */
    ret = config_envelope_unpack(CONFIG_DOMAIN_NET_ETH, env_buf, env_len,
                                 unpacked, 10, &unpacked_len);
    zassert_equal(ret, -ENOBUFS, "Should reject buffer too small");

    /* CRC corruption detection */
    env_buf[CONFIG_ENVELOPE_HEADER_SIZE + 5] ^= 0x55;
    ret = config_envelope_unpack(CONFIG_DOMAIN_NET_ETH, env_buf, env_len,
                                 unpacked, sizeof(unpacked), &unpacked_len);
    zassert_equal(ret, -EBADMSG, "Should detect CRC corruption");

    /* Magic corruption detection */
    env_buf[CONFIG_ENVELOPE_HEADER_SIZE + 5] ^= 0x55; /* restore */
    env_buf[0] ^= 0xFF; /* corrupt magic */
    ret = config_envelope_unpack(CONFIG_DOMAIN_NET_ETH, env_buf, env_len,
                                 unpacked, sizeof(unpacked), &unpacked_len);
    zassert_equal(ret, -EBADMSG, "Should detect magic mismatch");
}

ZTEST(storage_suite, test_thermal_energy_types_and_alignments)
{
    zassert_equal(sizeof(thermal_pair_mapping_t), 4U, "thermal_pair_mapping_t size must be 4");
    zassert_equal(sizeof(thermal_hourly_record_t), THERMAL_HOURLY_RECORD_EXPECTED_SIZE,
                  "thermal_hourly_record_t size must match THERMAL_HOURLY_RECORD_EXPECTED_SIZE");
    zassert_equal(sizeof(config_thermal_site_t), 184U, "config_thermal_site_t size must be 184");
    zassert_equal(sizeof(config_thermal_baseline_t), 216U, "config_thermal_baseline_t size must be 216");

    zassert_equal(THERMAL_MAX_PAIRS, 16U, "THERMAL_MAX_PAIRS == 16");
    zassert_equal(THERMAL_MAX_ROOM_PROBES, 16U, "THERMAL_MAX_ROOM_PROBES == 16");
    zassert_equal(THERMAL_MAX_OUTDOOR_PROBES, 8U, "THERMAL_MAX_OUTDOOR_PROBES == 8");
    zassert_equal(THERMAL_MAX_HVAC_UNITS, 8U, "THERMAL_MAX_HVAC_UNITS == 8");
    zassert_equal(THERMAL_HOURLY_HISTORY_CAPACITY, 720U, "THERMAL_HOURLY_HISTORY_CAPACITY == 720");

    zassert_equal(QUALITY_VALID, 0, "QUALITY_VALID == 0");
    zassert_equal(QUALITY_SUSPECT, 1, "QUALITY_SUSPECT == 1");
    zassert_equal(QUALITY_FAILED, 2, "QUALITY_FAILED == 2");
    zassert_equal(QUALITY_UNAVAILABLE, 3, "QUALITY_UNAVAILABLE == 3");

    zassert_equal(COMPRESSOR_STATE_OFF, 0, "COMPRESSOR_STATE_OFF == 0");
    zassert_equal(COMPRESSOR_STATE_INRUSH_WAIT, 1, "COMPRESSOR_STATE_INRUSH_WAIT == 1");
    zassert_equal(COMPRESSOR_STATE_STEP_LOAD_CAPTURE, 2, "COMPRESSOR_STATE_STEP_LOAD_CAPTURE == 2");
    zassert_equal(COMPRESSOR_STATE_THERMAL_WARMUP, 3, "COMPRESSOR_STATE_THERMAL_WARMUP == 3");
    zassert_equal(COMPRESSOR_STATE_STEADY_STATE, 4, "COMPRESSOR_STATE_STEADY_STATE == 4");
}

ZTEST(storage_suite, test_thermal_site_config_defaults_and_validation)
{
    const config_thermal_site_t *def_site = default_config_get_thermal_site_cfg();
    zassert_not_null(def_site, "default thermal site config must not be null");
    zassert_true(config_thermal_site_is_valid(def_site), "default thermal site config must be valid");
    zassert_equal(def_site->configured_pairs_count, 6U, "default pairs count should be 6");
    zassert_equal(def_site->configured_room_probes, 2U, "default room probes should be 2");
    zassert_equal(def_site->configured_outdoor_probes, 2U, "default outdoor probes should be 2");
    zassert_equal(def_site->required_redundancy_units, 2U, "default redundancy units should be 2");
    zassert_equal(def_site->site_altitude_m, 0, "default altitude should be 0");

    /* Validation edge cases */
    zassert_false(config_thermal_site_is_valid(NULL), "NULL config must be invalid");

    config_thermal_site_t bad_site = *def_site;
    bad_site.configured_pairs_count = 0U;
    zassert_false(config_thermal_site_is_valid(&bad_site), "0 pairs must be invalid");

    bad_site = *def_site;
    bad_site.configured_pairs_count = 17U;
    zassert_false(config_thermal_site_is_valid(&bad_site), ">16 pairs must be invalid");

    bad_site = *def_site;
    bad_site.pair_mapping_table[0].supply_flat_id = bad_site.pair_mapping_table[0].return_flat_id;
    zassert_false(config_thermal_site_is_valid(&bad_site), "Identical supply and return flat IDs must be invalid");

    bad_site = *def_site;
    bad_site.site_altitude_m = 5000;
    zassert_false(config_thermal_site_is_valid(&bad_site), "Altitude > 4000m must be invalid");

    bad_site = *def_site;
    bad_site.nominal_airflow_m3h[0] = 100.0f;
    zassert_false(config_thermal_site_is_valid(&bad_site), "Airflow < 500 must be invalid");

    /* Envelope pack / unpack and fake storage save / load */
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();
    zassert_equal(i_config_storage_init(&port), 0, "Init failed");

    uint8_t pack_buf[sizeof(config_envelope_header_t) + sizeof(config_thermal_site_t)];
    size_t packed_len = 0U;
    zassert_equal(config_envelope_pack(CONFIG_DOMAIN_THERMAL_SITE, def_site, sizeof(*def_site),
                                       pack_buf, sizeof(pack_buf), &packed_len), 0, "Pack failed");
    zassert_equal(packed_len, sizeof(config_envelope_header_t) + sizeof(*def_site), "Packed len mismatch");

    zassert_equal(i_config_storage_save(&port, CONFIG_DOMAIN_THERMAL_SITE, pack_buf, packed_len), 0, "Save failed");

    uint8_t load_buf[sizeof(pack_buf)];
    size_t loaded_len = 0U;
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_THERMAL_SITE, load_buf, sizeof(load_buf), &loaded_len), 0, "Load failed");
    zassert_equal(loaded_len, packed_len, "Loaded len mismatch");

    config_thermal_site_t unpacked_site;
    size_t unpacked_len = 0U;
    zassert_equal(config_envelope_unpack(CONFIG_DOMAIN_THERMAL_SITE, load_buf, loaded_len,
                                         &unpacked_site, sizeof(unpacked_site), &unpacked_len), 0, "Unpack failed");
    zassert_equal(unpacked_len, sizeof(config_thermal_site_t), "Unpacked size mismatch");
    zassert_mem_equal(&unpacked_site, def_site, sizeof(config_thermal_site_t), "Unpacked data mismatch");
}

ZTEST(storage_suite, test_thermal_baseline_config_defaults_and_validation)
{
    const config_thermal_baseline_t *def_base = default_config_get_thermal_baseline_cfg();
    zassert_not_null(def_base, "default baseline config must not be null");
    zassert_true(config_thermal_baseline_is_valid(def_base), "default baseline config must be valid");
    zassert_equal(def_base->is_valid, 0U, "default baseline should be untrained (is_valid == 0)");
    zassert_equal(def_base->training_hours_accumulated, 0U, "default training hours should be 0");

    /* Validation edge cases */
    zassert_false(config_thermal_baseline_is_valid(NULL), "NULL baseline config must be invalid");

    config_thermal_baseline_t trained_base = *def_base;
    trained_base.is_valid = 1U;
    trained_base.training_hours_accumulated = 168U;
    trained_base.baseline_exergetic_efficiency = 42.0f;
    zassert_true(config_thermal_baseline_is_valid(&trained_base), "168h trained baseline must be valid");

    config_thermal_baseline_t bad_base = trained_base;
    bad_base.training_hours_accumulated = 100U;
    zassert_false(config_thermal_baseline_is_valid(&bad_base), "Trained baseline with < 168h must be invalid");

    bad_base = trained_base;
    bad_base.baseline_exergetic_efficiency = 10.0f;
    zassert_false(config_thermal_baseline_is_valid(&bad_base), "Trained baseline with exergy < 20% must be invalid");

    bad_base = trained_base;
    bad_base.is_valid = 5U;
    zassert_false(config_thermal_baseline_is_valid(&bad_base), "is_valid != 0 and != 1 must be invalid");

    /* Envelope pack / unpack and fake storage save / load */
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();
    zassert_equal(i_config_storage_init(&port), 0, "Init failed");

    uint8_t pack_buf[sizeof(config_envelope_header_t) + sizeof(config_thermal_baseline_t)];
    size_t packed_len = 0U;
    zassert_equal(config_envelope_pack(CONFIG_DOMAIN_THERMAL_BASELINE, &trained_base, sizeof(trained_base),
                                       pack_buf, sizeof(pack_buf), &packed_len), 0, "Pack failed");

    zassert_equal(i_config_storage_save(&port, CONFIG_DOMAIN_THERMAL_BASELINE, pack_buf, packed_len), 0, "Save failed");

    uint8_t load_buf[sizeof(pack_buf)];
    size_t loaded_len = 0U;
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_THERMAL_BASELINE, load_buf, sizeof(load_buf), &loaded_len), 0, "Load failed");

    config_thermal_baseline_t unpacked_base;
    size_t unpacked_len = 0U;
    zassert_equal(config_envelope_unpack(CONFIG_DOMAIN_THERMAL_BASELINE, load_buf, loaded_len,
                                         &unpacked_base, sizeof(unpacked_base), &unpacked_len), 0, "Unpack failed");
    zassert_equal(unpacked_len, sizeof(config_thermal_baseline_t), "Unpacked size mismatch");
    zassert_mem_equal(&unpacked_base, &trained_base, sizeof(config_thermal_baseline_t), "Unpacked data mismatch");
}
